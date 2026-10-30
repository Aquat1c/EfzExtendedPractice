#include "../include/gui/custom_menu/screens.h"
#include "../include/gui/custom_menu/anim.h"
#include "../include/gui/custom_menu/layout.h"
#include "../include/gui/custom_menu/theme.h"
#include "../include/gui/custom_menu/scale.h"
#include "../include/gui/custom_menu/input.h"
#include "../include/gui/custom_menu/sound.h"
#include "../include/core/constants.h"
#include "../include/utils/config.h"
#include "../include/game/collision_display.h"
#include "../include/utils/i18n.h"
#include "../include/utils/utilities.h"     // detectedBindings
#include "../include/utils/xinput_shim.h"
#include "../3rdparty/imgui/imgui.h"

#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

namespace CustomMenu::Screens {

namespace {

int ActionNormalBase(int action) {
    if (action >= ACTION_5A && action <= ACTION_2D) return (action / 4) * 4;
    if (action >= ACTION_JA && action <= ACTION_JD) return ACTION_JA;
    if (action >= ACTION_6A && action <= ACTION_4D) return ACTION_6A + ((action - ACTION_6A) / 4) * 4;
    return -1;
}

bool PopupActionUsesButtonStrength(int action) {
    switch (action) {
        case ACTION_QCF:
        case ACTION_DP:
        case ACTION_QCB:
        case ACTION_421:
        case ACTION_SUPER1:
        case ACTION_SUPER2:
        case ACTION_236236:
        case ACTION_214214:
        case ACTION_641236:
        case ACTION_463214:
        case ACTION_412:
        case ACTION_22:
        case ACTION_4123641236:
        case ACTION_6321463214:
        case ACTION_1X:
        case ACTION_3X:
        case ACTION_J2X:
        case ACTION_J6X:
        case ACTION_66X:
        case ACTION_662X:
        case ACTION_664X:
            return true;
        default:
            return false;
    }
}

const char* const kGroupedActionChoices[] = {
    "5X (STANDING)",
    "2X (CROUCHING)",
    "jX (AIR)",
    "QCF (236)",
    "DP (623)",
    "QCB (214)",
    "421",
    "41236",
    "2141236",
    "236236",
    "214214",
    "641236",
    "463214",
    "412",
    "22",
    "4123641236",
    "6321463214",
    "JUMP",
    "BACKDASH",
    "FORWARD DASH",
    "BLOCK",
    "FINAL MEMORY",
    "6X (FORWARD)",
    "4X (BACK)"
};

const int kGroupedActionValues[] = {
    ACTION_5A,
    ACTION_2A,
    ACTION_JA,
    ACTION_QCF,
    ACTION_DP,
    ACTION_QCB,
    ACTION_421,
    ACTION_SUPER1,
    ACTION_SUPER2,
    ACTION_236236,
    ACTION_214214,
    ACTION_641236,
    ACTION_463214,
    ACTION_412,
    ACTION_22,
    ACTION_4123641236,
    ACTION_6321463214,
    ACTION_JUMP,
    ACTION_BACKDASH,
    ACTION_FORWARD_DASH,
    ACTION_BLOCK,
    ACTION_FINAL_MEMORY,
    ACTION_6A,
    ACTION_4A
};

constexpr int kGroupedActionCount = sizeof(kGroupedActionValues) / sizeof(kGroupedActionValues[0]);

int ActionToGroupedChoiceIndex(int action) {
    const int grouped = ActionNormalBase(action) >= 0 ? ActionNormalBase(action) : action;
    for (int i = 0; i < kGroupedActionCount; ++i) {
        if (kGroupedActionValues[i] == grouped) {
            return i;
        }
    }
    return 0;
}

int ActionStrengthSecondaryCount(const Row& r) {
    if (!r.choiceIdxPtr) return 0;

    const int action = *r.choiceIdxPtr;
    if (ActionNormalBase(action) >= 0 || PopupActionUsesButtonStrength(action)) {
        return 4;
    }
    if (action == ACTION_JUMP) {
        return 3;
    }
    return 0;
}

    // Info rows are focusable so keyboard nav can scroll paragraph text (Activate is a no-op on them).
    // Headers and Spacers stay non-focusable so the cursor doesn't park on a divider.
bool RowIsFocusable(const Row& r) {
    switch (r.kind) {
        case RowKind::Header:
        case RowKind::Spacer:
        case RowKind::Custom:
            return false;
        default:
            return true;
    }
}

    // Focused Info rows keep an understated look so pages don't look cluttered.
bool RowDrawsFocusChrome(const Row& r) {
    return r.kind != RowKind::Info;
}

bool RowHidden(const Row& r) {
    return r.isHidden && r.isHidden();
}

bool RowDisabled(const Row& r) {
    return r.isDisabled && r.isDisabled();
}

std::string g_currentHelpText;

bool TextEquals(const char* a, const char* b) {
    return a && b && strcmp(a, b) == 0;
}

bool TextContains(const char* text, const char* needle) {
    return text && needle && strstr(text, needle) != nullptr;
}

const char* CleanLabel(const char* label) {
    if (!label) return "";
    while (*label == ' ' || *label == '\t') {
        ++label;
    }
    return label;
}

bool LabelEquals(const char* label, const char* expected) {
    return TextEquals(CleanLabel(label), expected);
}

bool LabelContains(const char* label, const char* needle) {
    return TextContains(CleanLabel(label), needle);
}

const char* DefaultActionHelpText(const char* label) {
    label = CleanLabel(label);
    if (!label || !*label) return "Runs this menu command.";

    if (TextEquals(label, "TELEPORT") || TextEquals(label, "LOAD / TELEPORT")) return "Sets the input used to return to your saved position.";
    if (TextEquals(label, "SAVE POSITION")) return "Sets the input used to store the current training position.";
    if (TextEquals(label, "TOGGLE STATS")) return "Sets the input used to show or hide practice statistics.";
    if (TextEquals(label, "RESET COUNTER")) return "Sets the input used to clear the current counter display.";
    if (TextEquals(label, "SWITCH PLAYERS")) return "Sets the input used to swap which side you control.";
    if (TextEquals(label, "SWAP POSITIONS")) return "Sets the input used to exchange player positions.";
    if (TextEquals(label, "SWAP CUSTOM KEY")) return "Sets the input used for the custom position-swap command.";

    if (TextEquals(label, "MACRO RECORD")) return "Sets the input used to start and finish recording macros.";
    if (TextEquals(label, "MACRO PLAY")) return "Sets the input used to play the selected macro slot.";
    if (TextEquals(label, "MACRO NEXT SLOT")) return "Sets the input used to advance the active macro slot.";
    if (TextEquals(label, "RECORD (TOGGLE)")) return "Starts pre-recording, starts capture, then saves the macro on the next press.";
    if (TextEquals(label, "PLAY")) return "Plays the selected macro slot once.";
    if (TextEquals(label, "STOP")) return "Stops any macro playback or recording in progress.";
    if (TextEquals(label, "PREV SLOT")) return "Moves to the previous macro slot.";
    if (TextEquals(label, "NEXT SLOT")) return "Moves to the next macro slot.";
    if (TextEquals(label, "EDIT TEXT")) return "Opens the serialized macro text editor for the selected slot.";
    if (TextEquals(label, "APPLY TO SLOT")) return "Writes the edited macro text into the selected slot.";
    if (TextEquals(label, "RELOAD FROM SLOT")) return "Restores the editor text from the selected macro slot.";
    if (TextEquals(label, "CLEAR SLOT")) return "Clears the selected macro slot.";
    if (TextEquals(label, "COPY")) return "Copies the serialized macro text to the clipboard.";
    if (TextEquals(label, "PASTE")) return "Pastes macro text from the clipboard into the editor.";
    if (TextEquals(label, "UNDO")) return "Restores the previous macro text edit.";
    if (TextEquals(label, "REDO")) return "Re-applies the next macro text edit.";
    if (TextEquals(label, "INSERT SAMPLE")) return "Inserts a small example macro into the editor.";
    if (TextEquals(label, "GUIDE")) return "Opens the macro guide page.";

    if (TextEquals(label, "SAVE PRACTICE SNAPSHOT")) return "Sets the input used to save the Practice snapshot.";
    if (TextEquals(label, "LOAD PRACTICE SNAPSHOT")) return "Sets the input used to load the Practice snapshot.";
    if (TextEquals(label, "SAVESTATE SAVE")) return "Sets the raw key code used to save the Practice snapshot.";
    if (TextEquals(label, "SAVESTATE LOAD")) return "Sets the raw key code used to load the Practice snapshot.";

    if (TextEquals(label, "UI ACCEPT")) return "Sets the menu confirm input.";
    if (TextEquals(label, "UI REFRESH")) return "Sets the menu refresh input.";
    if (TextEquals(label, "UI EXIT")) return "Sets the menu back or close input.";
    if (TextEquals(label, "FRAMESTEP PAUSE")) return "Sets the input used to pause frame stepping.";
    if (TextEquals(label, "FRAMESTEP STEP")) return "Sets the input used to advance one frame step.";
    if (TextEquals(label, "TOGGLE MENU")) return "Sets the controller button that opens this menu.";
    if (TextEquals(label, "OPEN MENU")) return "Sets the keyboard key that opens this menu. Any key other than Esc gives Esc back to EFZ.";
    if (TextEquals(label, "USE ESC TO OPEN MENU")) return "Sets the menu key back to Esc.";
    if (TextEquals(label, "TOP TAB PREVIOUS")) return "Sets the controller button for the previous top tab.";
    if (TextEquals(label, "TOP TAB NEXT")) return "Sets the controller button for the next top tab.";
    if (TextEquals(label, "SUBTAB PREVIOUS")) return "Sets the controller button for the previous subtab.";
    if (TextEquals(label, "SUBTAB NEXT")) return "Sets the controller button for the next subtab.";
    if (TextEquals(label, "SAVE CONTROLLER BINDS")) return "Saves controller bindings to disk.";
    if (TextEquals(label, "SAVE ALL TO DISK")) return "Saves all menu settings to the config file.";
    if (TextEquals(label, "RELOAD FROM DISK")) return "Reloads menu settings from the config file.";

    if (TextEquals(label, "TOGGLE SWITCH PLAYERS")) return "Flips the active player-control side for debugging.";
    if (TextEquals(label, "CANCEL P1 RF FREEZE")) return "Releases Player 1 from the debug RF freeze state.";
    if (TextEquals(label, "CANCEL P2 RF FREEZE")) return "Releases Player 2 from the debug RF freeze state.";
    if (TextEquals(label, "PLAY BGM")) return "Starts the selected music track immediately.";
    if (TextEquals(label, "STOP BGM")) return "Stops the currently playing music track.";
    if (TextEquals(label, "RUN P1 FINAL MEMORY")) return "Triggers Player 1's Final Memory for testing.";
    if (TextEquals(label, "RUN P2 FINAL MEMORY")) return "Triggers Player 2's Final Memory for testing.";

    if (TextEquals(label, "EFZ WIKI")) return "Opens the Eternal Fighter Zero wiki in your browser.";
    if (TextEquals(label, "TRAINING MODE WIKI")) return "Opens the training mode documentation in your browser.";
    if (TextEquals(label, "EFZ GLOBAL DISCORD")) return "Opens the community Discord invite in your browser.";
    if (TextContains(label, " WIKI") || TextContains(label, "OPEN P1 WIKI") || TextContains(label, "OPEN P2 WIKI")) {
        return "Opens the current character's wiki page in your browser.";
    }
    if (TextEquals(label, "OPEN GITHUB RELEASES")) return "Opens the release page for downloading updates.";

    if (TextEquals(label, "FORCE SUMMON")) return "Spawns the character-specific helper immediately.";
    if (TextEquals(label, "FORCE DESPAWN")) return "Removes the character-specific helper immediately.";
    if (TextEquals(label, "APPLY GHOST POSITION")) return "Moves the ghost helper to the configured position.";
    if (TextEquals(label, "APPLY MICHIRU POSITION")) return "Moves Michiru to the configured position.";
    if (TextEquals(label, "REFRESH CHARACTER DATA")) return "Re-detects the current matchup and available character controls.";

    if (TextEquals(label, "CHANGE CHARACTERS / STAGE")) return "Opens match hotswap for characters, palettes, stage, and music.";
    if (TextEquals(label, "CHARACTER SETTINGS")) return "Opens controls specific to the current characters.";
    if (TextEquals(label, "ABOUT")) return "Opens version and project information.";
    if (TextEquals(label, "SOUND SETTINGS")) return "Opens audio and music settings.";
    if (TextEquals(label, "EXIT TO CHARACTER SELECT")) return "Leaves Practice and returns to character select.";
    if (TextEquals(label, "EXIT TO TITLE SCREEN")) return "Leaves Practice and returns to the title screen.";
    if (TextEquals(label, "APPLY SELECTIONS")) return "Applies the selected hotswap changes to the current match.";

    if (LabelContains(label, "SAVE")) return "Saves the selected data or setting.";
    if (LabelContains(label, "LOAD")) return "Loads the selected data or setting.";
    if (LabelContains(label, "OPEN")) return "Opens the selected destination.";
    if (LabelContains(label, "APPLY")) return "Applies the configured value to the live match.";
    if (LabelContains(label, "CLEAR")) return "Clears the selected value or slot.";
    if (LabelContains(label, "CANCEL")) return "Cancels the selected active state.";
    if (LabelContains(label, "FORCE")) return "Forces this character state immediately.";
    if (LabelContains(label, "EDIT")) return "Opens an editor for this setting.";
    if (LabelContains(label, "COPY")) return "Copies the selected data to the clipboard.";
    if (LabelContains(label, "PASTE")) return "Pastes clipboard data into this page.";
    if (LabelContains(label, "NEXT")) return "Moves to the next item in this group.";
    if (LabelContains(label, "PREV")) return "Moves to the previous item in this group.";
    if (LabelContains(label, "EXIT")) return "Leaves the current mode and returns to the selected destination.";
    if (LabelContains(label, "TOGGLE")) return "Switches the selected command state.";
    return "Runs this menu command.";
}

const char* DefaultToggleHelpText(const char* label) {
    label = CleanLabel(label);
    if (LabelEquals(label, "HITBOXES")) return "Shows attack boxes on both fighters and their projectiles.";
    if (LabelEquals(label, "HURTBOXES")) return "Shows hurtboxes on both fighters and their projectiles.";
    if (LabelEquals(label, "COLLISION BOXES")) return "Shows pushboxes on both fighters and collision boxes on their projectiles.";
    if (LabelEquals(label, "P1 HITBOXES")) return "Shows Player 1's attack boxes, including projectiles owned by Player 1.";
    if (LabelEquals(label, "P2 HITBOXES")) return "Shows Player 2's attack boxes, including projectiles owned by Player 2.";
    if (LabelEquals(label, "P1 HURTBOXES")) return "Shows hurtboxes for Player 1 and Player 1's projectiles.";
    if (LabelEquals(label, "P2 HURTBOXES")) return "Shows hurtboxes for Player 2 and Player 2's projectiles.";
    if (LabelEquals(label, "P1 COLLISION BOXES")) return "Shows Player 1's pushbox and Player 1's projectile collision boxes.";
    if (LabelEquals(label, "P2 COLLISION BOXES")) return "Shows Player 2's pushbox and Player 2's projectile collision boxes.";
    if (LabelEquals(label, "PROJECTILE INTERACTIONS")) return "Turns on the projectile overlay. The sub-layer rows below pick what it draws.";
    if (LabelEquals(label, "PROJECTILE BOXES")) return "Shows projectile hit and collision boxes.";
    if (LabelEquals(label, "ORIGIN / RANGE DOTS")) return "Shows projectile anchors and range markers.";
    if (LabelEquals(label, "INTERSECTION BOXES")) return "Highlights the area where two projectiles' collision boxes overlap.";
    if (LabelEquals(label, "USE CUSTOM MENU")) return "Uses this EFZ-style menu instead of the advanced ImGui menu.";
    if (LabelEquals(label, "PRACTICE OVERLAY HINT")) return "Shows a one-time reminder of the key that opens this menu when you enter Practice.";
    if (LabelEquals(label, "CR: BOTH NEUTRAL REQD")) return "Requires both players to return to neutral before continuous recovery restores values.";
    if (LabelEquals(label, "AUTO-FIX HP<=0")) return "Refills a fighter to 9999 HP if it is left at 0 or below while neutral.";
    if (LabelEquals(label, "FREEZE RF AFTER CR")) return "Keeps RF fixed after continuous recovery restores it.";
    if (LabelEquals(label, "FREEZE RF ONLY NEUTRAL")) return "Freezes RF only while that fighter is neutral. CR: Both Neutral Reqd makes it wait for both.";
    if (LabelEquals(label, "CUSTOM SWAP KEY")) return "Enables a separate custom hotkey for position swapping.";
    if (LabelEquals(label, "ENABLE P2 CONTROL")) return "Enables P2 controls in Practice mode; F6/F7 stance and blocking hotkeys are unavailable while this is on.";
    if (LabelEquals(label, "RANDOM BLOCK")) return "Coin-flips the dummy's guard each frame inside the Dummy Auto-Block window, so some hits land.";
    if (LabelEquals(label, "ADAPTIVE STANCE")) return "Automatically switches the dummy between standing and crouching guard depending on the incoming attack.";
    if (LabelEquals(label, "ALWAYS RECOIL GUARD")) return "Keeps the dummy's Recoil Guard armed. Normal RG rules still apply - no RG on consecutive rapid hits while grounded.";
    if (LabelEquals(label, "RANDOM RECOIL GUARD")) return "Arms the dummy's Recoil Guard on a coin flip each frame, so some blocks come out as RG.";
    if (LabelEquals(label, "COUNTER RG")) return "Tries to Recoil Guard back after your Recoil Guard. Unavailable while Always Recoil Guard is on.";
    if (LabelEquals(label, "AUTO-JUMP")) return "Makes the selected side jump on its own and again on every landing.";
    if (LabelEquals(label, "ENABLE")) return "Turns on the selected auto-action trigger.";
    if (LabelEquals(label, "RANDOM POOL")) return "Lets this trigger pick randomly from the selected action pool.";
    if (LabelEquals(label, "RANDOMIZE TRIGGERS")) return "Gives every enabled trigger a 50/50 chance to skip, so your setup is not always answered.";
    if (LabelEquals(label, "PRE-BUFFER WAKEUP")) return "Starts a wakeup macro early so its first attack is buffered. Wakeup specials buffer without it.";
    if (LabelEquals(label, "ENABLE OVERLAY")) return "Shows this overlay during play.";
    if (LabelEquals(label, "SHOW DETAIL ROW")) return "Adds the extra detail line to the combo statistics overlay.";
    if (LabelEquals(label, "KEEP FINAL SUMMARY")) return "Leaves the finished combo summary visible after the combo ends.";
    if (LabelEquals(label, "HIDE WITH MENU")) return "Hides this overlay while the custom menu is open.";
    if (LabelEquals(label, "RESUME AFTER MENU")) return "Restores this overlay after closing the custom menu.";
    if (LabelEquals(label, "SHOW RF MULTIPLIER")) return "Displays the RF scaling multiplier in combo statistics.";
    if (LabelEquals(label, "SHOW RAW SCALE")) return "Displays raw combo scaling values in combo statistics.";
    if (LabelEquals(label, "FINAL MEMORY AT ANY HP")) return "Allows Final Memory without the normal 3332 HP restriction.";
    if (LabelEquals(label, "FRAME ADVANTAGE OVERLAY")) return "Shows frame advantage results after blocked or hit attacks.";
    if (LabelEquals(label, "SHOW FRAME BAR")) return "Shows the per-player timing timeline near the bottom of the screen.";
    if (LabelEquals(label, "ENABLE FRAMESTEP")) return "Enables pausing and stepping the game frame by frame.";
    if (LabelEquals(label, "SUPPRESS REVIVAL STEP")) return "Prevents Revival's own framestep from also handling step input.";
    if (LabelEquals(label, "INCLUDE BUFFERS")) return "Includes input-buffer data in the macro text on this page.";
    if (LabelEquals(label, "DETAILED LOGGING")) return "Writes extra training-mode diagnostics to the log.";
    if (LabelEquals(label, "DEBUG FILE LOG")) return "Writes debug messages to the external log file.";
    if (LabelEquals(label, "FPS DIAGNOSTICS")) return "Records frame pacing diagnostics for troubleshooting.";
    if (LabelEquals(label, "SHOW DEBUG CONSOLE")) return "Opens the debug console window on startup.";
    if (LabelEquals(label, "CHAR SELECT LOGGER")) return "Logs character-select state while testing hotswap issues.";
    if (LabelEquals(label, "LOG CONTROLLER INPUT")) return "Logs controller input packets seen by the mod.";
    if (LabelEquals(label, "LOG DETAILED FA")) return "Logs detailed frame-advantage state transitions.";
    if (LabelEquals(label, "OVERLAY DEBUG BORDERS")) return "Draws borders around overlay layout regions.";
    if (LabelEquals(label, "RG DEBUG TOASTS")) return "Shows small on-screen messages when RG helpers change state.";
    if (LabelEquals(label, "PLAYER 1 CUSTOM PALETTE")) return "Uses Player 1's custom palette file for the hotswap. Only works when the file is present locally.";
    if (LabelEquals(label, "PLAYER 2 CUSTOM PALETTE")) return "Uses Player 2's custom palette file for the hotswap. Only works when the file is present locally.";
    if (LabelEquals(label, "NOTE TRIGGER RANGES")) return "Shows Mizuka's note trigger ranges during play.";
    if (LabelEquals(label, "AFFECTED NOTES")) return "Tints a Mizuka note pale yellow once it has been set off.";
    if (LabelEquals(label, "MINAGI PROJECTILES -> MICHIRU")) return "Turns some of Minagi's projectiles into extra Michiru puppets.";
    if (LabelEquals(label, "BAREHANDED MODE")) return "Forces Rumi into her barehanded state.";
    if (LabelEquals(label, "KIMCHI ACTIVE")) return "Keeps Rumi's kimchi state active.";
    if (LabelEquals(label, "ENLIGHTENED")) return "Forces Doppel into her enlightened Gold Doppel form.";
    if (LabelEquals(label, "ALWAYS READIED")) return "Michiru goes into the readied stance as soon as she becomes neutral.";

    if (LabelContains(label, "INFINITE")) return "Keeps the related character resource active instead of letting it expire.";
    if (LabelContains(label, "LOCK")) return "Keeps this character state fixed at the configured value.";
    if (LabelContains(label, "FREEZE")) return "Prevents this value or cycle from advancing normally.";
    if (LabelContains(label, "SHOW")) return "Displays this extra visual information during play.";
    if (LabelContains(label, "CUSTOM PALETTE")) return "Uses a custom palette for the selected player. Only works when the file is present locally.";
    if (LabelContains(label, "AGGRESSIVE")) return "Makes the helper use its more aggressive behavior.";
    if (LabelContains(label, "COOLDOWN")) return "Removes the usual cooldown restriction for this character action.";
    if (LabelContains(label, "ACTIVE")) return "Forces or preserves the named active state.";
    if (LabelContains(label, "ENABLE")) return "Enables this feature for the current practice setup.";
    return "Turns this setting on or off.";
}

const char* DefaultNumberHelpText(const char* label) {
    label = CleanLabel(label);
    if (LabelEquals(label, "UI SCALE")) return "Changes the size of the custom menu.";
    if (LabelEquals(label, "BOX FILL ALPHA")) return "Adjusts how opaque filled collision boxes appear.";
    if (LabelEquals(label, "BGM VOLUME")) return "Adjusts background music volume as a percentage.";
    if (LabelEquals(label, "SE VOLUME")) return "Adjusts sound-effect volume as a percentage.";
    if (LabelEquals(label, "CR NEUTRAL DELAY (MS)")) return "Sets how long both players must stay neutral before continuous recovery applies.";
    if (LabelEquals(label, "AUTO-BLOCK TIMEOUT (MS)")) return "Sets how long neutral must last before First Hit and After Hit reset. 0 resets as soon as neutral starts.";
    if (LabelEquals(label, "CURRENT SLOT")) return "Chooses the active macro slot.";
    if (LabelEquals(label, "AIRTECH DELAY")) return "Waits this many frames after the dummy can tech before it airtechs.";
    if (LabelEquals(label, "DELAY")) return "Waits this many frames before the dummy starts the selected response.";
    if (LabelEquals(label, "RF AMOUNT")) return "Sets the RF value restored by custom F4 recovery.";
    if (LabelEquals(label, "FA DURATION (SEC)")) return "Sets how long frame advantage results stay on screen.";
    if (LabelEquals(label, "SUMMARY TIME")) return "Sets how long the final combo summary remains visible.";

    if (LabelEquals(label, "BLOOD STOCK")) return "Sets Ikumi's blood stock, 0 to 8.";
    if (LabelEquals(label, "FEATHERS")) return "Sets Misuzu's feather count, 0 to 3.";
    if (LabelEquals(label, "JAM COUNT")) return "Sets Neyuki's stored jam count, 0 to 9.";
    if (LabelEquals(label, "MAGIC")) return "Sets Kano's stored magic, 0 to 10000.";
    if (LabelEquals(label, "GHOST TIME")) return "Sets how long Mai's ghost remains active.";

    if (LabelContains(label, "GENOCIDE")) return "Sets Ikumi's genocide timer.";
    if (LabelContains(label, "SNOWBUNNY")) return "Sets how long Nayuki's snow bunnies remain active.";
    if (LabelContains(label, "CHARGE")) return "Sets Mai's ghost charge timer.";
    if (LabelContains(label, "AWAKEN")) return "Sets the awakening timer for this character state.";
    if (LabelContains(label, "POISON TIMER")) return "Sets how long Misuzu's poison lasts.";
    if (LabelContains(label, "KIMCHI TIMER")) return "Sets how long Rumi's kimchi effect lasts.";
    // Generic fallback for any other "... TIMER" row; must stay BELOW the per-character
    // rules (LabelContains is first-match and those rows also end in TIMER).
    if (LabelContains(label, "TIMER")) return "Sets how long this character state lasts.";
    if (LabelContains(label, "TARGET X")) return "Sets the horizontal spot Mai's ghost or Michiru moves to when you run the Apply row.";
    if (LabelContains(label, "TARGET Y")) return "Sets the vertical spot Mai's ghost or Michiru moves to when you run the Apply row.";
    if (LabelContains(label, "BLOOD")) return "Sets Ikumi's stored blood resource.";
    if (LabelContains(label, "LEVEL GAUGE")) return "Sets Ikumi's level gauge, 0 to 99.";
    if (LabelContains(label, "POISON LEVEL")) return "Sets Misuzu's poison strength.";
    return "Changes this numeric setting.";
}

const char* DefaultChoiceHelpText(const char* label) {
    label = CleanLabel(label);
    if (LabelEquals(label, "UI FONT (ADVANCED MENU)")) return "Chooses which font the advanced ImGui menu uses.";
    if (LabelEquals(label, "TRIGGER")) return "Chooses which auto-action timing you are editing on this page.";
    if (LabelEquals(label, "ELEMENT")) return "Sets Mishio's element to none, fire, or lightning, or forces her awakened state.";
    if (LabelEquals(label, "TIME-SLOW TRIGGER")) return "Shows which Akiko time-slow is running: none, A, B, or C.";
    if (LabelEquals(label, "STANCE")) return "Chooses Mio's short or long stance.";
    if (LabelEquals(label, "STATUS")) return "Chooses Mai's ghost or awakening state.";
    if (LabelEquals(label, "DUMMY AUTO-BLOCK")) return "Chooses when the dummy blocks: never, every hit, only the first hit, or only after the first hit.";
    if (LabelEquals(label, "DUMMY STANCE")) return "Sets the dummy's F6 stance. Adaptive Stance takes it over while Dummy Auto-Block is on.";
    if (LabelEquals(label, "AUTO-AIRTECH")) return "Makes the dummy air-recover forward or back once it can tech. Neutral turns Auto-Airtech off.";
    if (LabelEquals(label, "JUMP DIRECTION")) return "Sets neutral, forward, or back jump direction for Auto-Jump.";
    if (LabelEquals(label, "JUMP TARGET")) return "Chooses whether Auto-Jump applies to P1, P2, or both sides.";
    if (LabelContains(label, "PALETTE")) return "Chooses the character palette for the hotswap selection.";
    if (LabelContains(label, "AUTOMATIC HEALTH AND METER RECOVERY (F4)")) return "Uses the game's F4 recovery modes; unavailable while F5 recovery is active.";
    if (LabelEquals(label, "COLOR")) return "Chooses which IC color custom F4 recovery restores.";
    if (LabelContains(label, "PRESETS FOR RECOVERY (F5)")) return "Uses the game's F5 recovery preset; turn it off before changing F4.";
    if (LabelEquals(label, "DETAIL SOURCE")) return "Chooses whether combo detail uses live combo state or the last hit.";
    if (LabelEquals(label, "CELL STEP")) return "Chooses subframe cells or wider visual-frame cells for the framebar.";
    if (LabelEquals(label, "DETAIL")) return "Chooses how much timing detail the framebar draws.";
    if (LabelEquals(label, "STEP MODE")) return "Chooses full-frame or single-subframe steps while paused.";
    return "Changes this setting's selected mode.";
}

const char* DefaultDropdownHelpText(const char* label) {
    label = CleanLabel(label);
    if (LabelEquals(label, "CONTROLLER FOR MOD INPUTS")) return "Chooses which controller can trigger training-mode pad shortcuts.";
    if (LabelEquals(label, "BGM TRACK")) return "Chooses the music track to play.";
    if (LabelEquals(label, "ACTION")) return "Chooses the dummy response for the selected trigger.";
    if (LabelEquals(label, "MACRO SLOT")) return "Chooses a recorded macro slot to use instead of a single action.";
    if (LabelContains(label, "CHARACTER")) return "Chooses the character used by the hotswap selection.";
    if (LabelEquals(label, "STAGE")) return "Chooses the stage used by the hotswap selection.";
    if (LabelEquals(label, "OST")) return "Chooses the music used by the hotswap selection.";
    return "Opens a picker for this setting.";
}

const char* DefaultMaskHelpText(const char* label) {
    label = CleanLabel(label);
    if (LabelEquals(label, "ACTION POOL")) return "Chooses the exact move versions Random Pool may roll.";
    return "Chooses multiple allowed entries for this setting.";
}

const char* DefaultSubmenuHelpText(const char* label) {
    label = CleanLabel(label);
    if (LabelEquals(label, "INTERFACE")) return "Opens menu appearance and behavior settings.";
    if (LabelEquals(label, "DISPLAY")) return "Opens hitbox, hurtbox, and overlay display settings.";
    if (LabelEquals(label, "AUDIO")) return "Opens volume and music settings.";
    if (LabelEquals(label, "RECOVERY")) return "Opens continuous recovery and RF freeze settings.";
    if (LabelEquals(label, "PRACTICE")) return "Opens practice-mode behavior settings.";
    if (LabelEquals(label, "CONTINUOUS RECOVERY")) return "Opens per-player continuous recovery controls.";
    if (LabelEquals(label, "DISPLAY OVERLAYS")) return "Opens overlay visibility and detail settings.";
    if (LabelEquals(label, "HOTSWAP")) return "Opens character, stage, and music hotswap settings.";
    if (LabelEquals(label, "PLAYER VALUES")) return "Opens health, meter, RF, IC color, and position for both fighters in the current match.";
    if (LabelEquals(label, "GAMEPLAY")) return "Opens gameplay hotkey bindings.";
    if (LabelEquals(label, "SAVESTATE")) return "Opens savestate hotkey bindings.";
    if (LabelEquals(label, "MACROS")) return "Opens macro controls or macro hotkey bindings.";
    if (LabelEquals(label, "MENU CONTROL")) return "Opens custom menu navigation bindings.";
    if (LabelEquals(label, "RAW VK CODES")) return "Opens direct virtual-key code editing.";
    if (LabelEquals(label, "CONTROLLER")) return "Opens controller button bindings.";
    if (LabelEquals(label, "SWAP POSITIONS")) return "Opens the custom key binding for swapping player positions.";
    if (LabelEquals(label, "LOGGING")) return "Opens diagnostic logging toggles.";
    if (LabelEquals(label, "OVERLAYS")) return "Opens debug overlay toggles.";
    if (LabelEquals(label, "INPUT / RUNTIME")) return "Opens switch-player controls and RF freeze cancels.";
    if (LabelEquals(label, "BGM")) return "Opens music playback debug tools.";
    if (LabelEquals(label, "FINAL MEMORY")) return "Opens Final Memory test actions.";
    if (LabelEquals(label, "SERIALIZED MACRO")) return "Opens macro text import, export, and editing tools.";
    if (LabelEquals(label, "SLOT STATS")) return "Opens length, timing, and input stats for the current macro slot.";
    if (LabelEquals(label, "QUICK START")) return "Opens the quick-start help page.";
    if (LabelEquals(label, "POSITION TOOLS")) return "Opens help for saving, loading, and swapping positions.";
    if (LabelEquals(label, "MENU TIPS")) return "Opens help for navigating this menu.";
    if (LabelEquals(label, "BASICS")) return "Opens core training-mode usage notes.";
    if (LabelEquals(label, "PRACTICE SNAPSHOTS")) return "Opens help for savestate snapshots.";
    if (LabelEquals(label, "COMBO STATISTICS")) return "Opens combo statistics help.";
    if (LabelEquals(label, "FRAMEBAR")) return "Opens framebar timing help.";
    if (LabelEquals(label, "BOX DISPLAY")) return "Opens collision display help.";
    if (LabelEquals(label, "AUTO ACTIONS")) return "Opens auto-action setup help.";
    if (LabelEquals(label, "ISSUES")) return "Opens troubleshooting notes.";
    if (LabelContains(label, "HOTKEY")) return "Opens hotkey binding settings.";
    if (LabelContains(label, "PLAYER")) return "Opens controls for this player or character.";
    return "Opens this menu page.";
}

const char* DefaultRowHelpText(const Row& r) {
    const char* label = (r.label && *r.label) ? r.label : "This row";
    switch (r.kind) {
        case RowKind::Info:
            return "Read this note for context about the current page or setting.";
        case RowKind::Toggle:
            return DefaultToggleHelpText(label);
        case RowKind::IntNumber:
        case RowKind::IntSlider:
        case RowKind::FloatNumber:
        case RowKind::DoubleNumber:
            return DefaultNumberHelpText(label);
        case RowKind::Choices:
            return DefaultChoiceHelpText(label);
        case RowKind::ActionStrength:
            return "Choose an action, then adjust its strength or direction when the row supports it.";
        case RowKind::TriggerButton:
            return "Choose the button, direction, or dash follow-up used by this auto-action.";
        case RowKind::Dropdown:
            return DefaultDropdownHelpText(label);
        case RowKind::MaskPicker:
            return DefaultMaskHelpText(label);
        case RowKind::Submenu:
            return DefaultSubmenuHelpText(label);
        case RowKind::Action:
            return DefaultActionHelpText(label);
        default:
            return "Shows contextual information for this row.";
    }
}

void SetCurrentHelpFromRow(const Row* rows, int rowCount, int focus) {
    g_currentHelpText.clear();
    if (!rows || rowCount <= 0 || focus < 0 || focus >= rowCount) return;
    const Row& r = rows[focus];
    const char* text = (r.helpText && *r.helpText) ? r.helpText : DefaultRowHelpText(r);
    if (text && *text) {
        g_currentHelpText = text;
    }
}

bool ShiftHeld() {
    if (!Input::IsGameWindowActive()) return false;
    return (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
}

constexpr float kInfoPadX = 8.0f;
constexpr float kInfoTextX = 18.0f;
constexpr float kInfoPadY = 1.0f;
constexpr float kInfoLineGap = 1.0f;

float InfoPadX() {
    return Scale::Snap((std::max)(4.0f, kInfoPadX * Scale::Get().layoutScale));
}

float InfoTextX() {
    return Scale::Snap((std::max)(10.0f, kInfoTextX * Scale::Get().layoutScale));
}

float InfoPadY() {
    return Scale::Snap((std::max)(1.0f, kInfoPadY * Scale::Get().layoutScale));
}

float InfoLineGap() {
    return Scale::Snap((std::max)(1.0f, kInfoLineGap * Scale::Get().layoutScale));
}

void PushWrappedLine(std::vector<std::string>& out, const std::string& line) {
    if (!line.empty()) {
        out.push_back(line);
    }
}

// Decodes one UTF-8 code point at p; returns its byte length (1 for invalid bytes).
int DecodeUtf8(const char* p, unsigned& cp) {
    const unsigned char c = static_cast<unsigned char>(p[0]);
    int len = 1;
    if (c >= 0xF0) { cp = c & 0x07; len = 4; }
    else if (c >= 0xE0) { cp = c & 0x0F; len = 3; }
    else if (c >= 0xC0) { cp = c & 0x1F; len = 2; }
    else { cp = c; return 1; }
    for (int i = 1; i < len; ++i) {
        const unsigned char cc = static_cast<unsigned char>(p[i]);
        if ((cc & 0xC0) != 0x80) { cp = c; return 1; }
        cp = (cp << 6) | (cc & 0x3F);
    }
    return len;
}

bool IsCjkBreakable(unsigned cp) {
    return (cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0xF900 && cp <= 0xFAFF) ||
           (cp >= 0xFF00 && cp <= 0xFFEF);
}

// Kinsoku: closing punctuation and small kana never start a line.
bool NoBreakBefore(unsigned cp) {
    switch (cp) {
        case 0x3001: case 0x3002: case 0x300D: case 0x300F: case 0x3011:
        case 0x30FC: case 0xFF09: case 0xFF0C: case 0xFF0E: case 0xFF1A:
        case 0xFF01: case 0xFF1F: case 0x3063: case 0x30C3: case 0x3083:
        case 0x3085: case 0x3087: case 0x30E3: case 0x30E5: case 0x30E7:
        case 0x3041: case 0x3043: case 0x3045: case 0x3047: case 0x3049:
        case 0x30A1: case 0x30A3: case 0x30A5: case 0x30A7: case 0x30A9:
        case ')': case ']': case ',': case '.': case ':': case ';': case '!': case '?':
            return true;
        default:
            return false;
    }
}

bool NoBreakAfter(unsigned cp) {
    return cp == 0x300C || cp == 0x300E || cp == 0x3010 || cp == 0xFF08 ||
           cp == '(' || cp == '[';
}

void WrapTextLine(ImFont* font, float px, const char* text, float maxW,
                  std::vector<std::string>& out) {
    if (!text || !*text) return;
    if (maxW <= 12.0f) {
        out.push_back(text);
        return;
    }

    std::string current;
    std::string word;
    bool spaceBeforeWord = false;
    unsigned prevCp = 0;

    auto flushWord = [&]() {
        if (word.empty()) return;
        if (current.empty()) {
            current = word;
        } else {
            std::string candidate = current;
            if (spaceBeforeWord) candidate.push_back(' ');
            candidate += word;
            if (Layout::MeasureTextW(font, px, candidate.c_str()) <= maxW) {
                current = candidate;
            } else {
                PushWrappedLine(out, current);
                current = word;
            }
        }
        word.clear();
        spaceBeforeWord = false;
    };

    for (const char* p = text; *p;) {
        const char c = *p;
        if (c == '\r') { ++p; continue; }
        if (c == '\n') {
            flushWord();
            PushWrappedLine(out, current);
            current.clear();
            spaceBeforeWord = false;
            prevCp = 0;
            ++p;
            continue;
        }
        if (c == ' ' || c == '\t') {
            flushWord();
            spaceBeforeWord = true;
            prevCp = 0;
            ++p;
            continue;
        }
        unsigned cp = 0;
        const int len = DecodeUtf8(p, cp);
        if (!word.empty() && (IsCjkBreakable(cp) || IsCjkBreakable(prevCp)) &&
            !NoBreakBefore(cp) && !NoBreakAfter(prevCp)) {
            flushWord();
        }
        word.append(p, static_cast<size_t>(len));
        prevCp = cp;
        p += len;
    }
    flushWord();
    PushWrappedLine(out, current);
}

struct InfoWrapCacheEntry {
    bool valid = false;
    const char* labelPtr = nullptr;
    int widthKey = 0;
    ImFont* font = nullptr;
    int fontPxKey = 0;
    std::string text;
    std::vector<std::string> lines;
    float height = 22.0f;
};

constexpr int kInfoWrapCacheSlots = 160;
InfoWrapCacheEntry g_infoWrapCache[kInfoWrapCacheSlots];
int g_infoWrapNextSlot = 0;

int InfoWrapWidthKey(float contentW) {
    return static_cast<int>(contentW * 4.0f + 0.5f);
}

int InfoWrapFontPxKey(float px) {
    return static_cast<int>(px * 16.0f + 0.5f);
}

float InfoTextBlockHeight(size_t lineCount, float px) {
    return static_cast<float>(lineCount) * px +
           static_cast<float>((lineCount > 0) ? lineCount - 1 : 0) * InfoLineGap();
}

void RebuildInfoWrapCacheEntry(InfoWrapCacheEntry& entry,
                               const Row& r,
                               float contentW,
                               ImFont* font,
                               float px,
                               int widthKey,
                               int fontPxKey) {
    const char* text = I18n::Tr(r.label ? r.label : "");
    entry.valid = true;
    entry.labelPtr = r.label ? r.label : "";
    entry.widthKey = widthKey;
    entry.font = font;
    entry.fontPxKey = fontPxKey;
    entry.text = text;
    entry.lines.clear();
    entry.lines.reserve(4);
    if (!*text) {
        entry.height = InfoPadY() * 2.0f;
        return;
    }

    const float textW = (std::max)(32.0f, contentW - InfoTextX() - InfoPadX());
    WrapTextLine(font, px, entry.text.c_str(), textW, entry.lines);
    if (entry.lines.empty()) entry.lines.push_back(entry.text);
    entry.height = Scale::Snap(InfoPadY() * 2.0f + InfoTextBlockHeight(entry.lines.size(), px));
}

const InfoWrapCacheEntry& GetInfoWrapCacheEntry(const Row& r, float contentW) {
    const char* label = r.label ? r.label : "";
    const char* text = I18n::Tr(label);
    ImFont* font = Layout::BodyFont();
    const float px = font ? font->FontSize : 13.0f;
    const int widthKey = InfoWrapWidthKey(contentW);
    const int fontPxKey = InfoWrapFontPxKey(px);

    int reusableSlot = -1;
    for (int i = 0; i < kInfoWrapCacheSlots; ++i) {
        InfoWrapCacheEntry& entry = g_infoWrapCache[i];
        if (!entry.valid) {
            reusableSlot = i;
            break;
        }
        if (entry.labelPtr == label &&
            entry.widthKey == widthKey &&
            entry.font == font &&
            entry.fontPxKey == fontPxKey) {
            if (entry.text == text) {
                return entry;
            }
            reusableSlot = i;
            break;
        }
    }

    if (reusableSlot < 0) {
        reusableSlot = g_infoWrapNextSlot;
        g_infoWrapNextSlot = (g_infoWrapNextSlot + 1) % kInfoWrapCacheSlots;
    }

    RebuildInfoWrapCacheEntry(g_infoWrapCache[reusableSlot],
                              r,
                              contentW,
                              font,
                              px,
                              widthKey,
                              fontPxKey);
    return g_infoWrapCache[reusableSlot];
}

float InfoRowHeight(const Row& r, float contentW) {
    return GetInfoWrapCacheEntry(r, contentW).height;
}

float RowPixelHeight(const Row& r, float contentW) {
    const Scale::Metrics& metrics = Scale::Get();
    if (r.kind == RowKind::Spacer) return Scale::Snap(metrics.rowHeight * 0.5f);
    if (r.kind == RowKind::Header) return metrics.rowHeight + metrics.sectionPadY;
    if (r.kind == RowKind::Info) return InfoRowHeight(r, contentW);
    if (r.kind == RowKind::Custom) {
        return r.customHeight > 0.0f
            ? Scale::Snap(r.customHeight * metrics.layoutScale)
            : metrics.rowHeight;
    }
    return metrics.rowHeight;
}

void FormatIntValue(const Row& r, char* buf, size_t bufSz) {
    _snprintf_s(buf, bufSz, _TRUNCATE, "%d", r.intPtr ? *r.intPtr : 0);
}

const char* FormatIntDisplayValue(const Row& r, char* buf, size_t bufSz) {
    if (r.valueFormatter) {
        const char* formatted = r.valueFormatter(r);
        if (formatted && *formatted) {
            return formatted;
        }
    }
    FormatIntValue(r, buf, bufSz);
    return buf;
}

float GetIntSliderProgress01(const Row& r) {
    if (!r.intPtr || r.intMax <= r.intMin) {
        return 0.0f;
    }
    const int value = *r.intPtr;
    const float denom = static_cast<float>(r.intMax - r.intMin);
    return static_cast<float>(value - r.intMin) / denom;
}

void FormatFloatValue(const Row& r, char* buf, size_t bufSz) {
    const char* fmt = r.floatFmt ? r.floatFmt : "%.2f";
    _snprintf_s(buf, bufSz, _TRUNCATE, fmt, r.floatPtr ? *r.floatPtr : 0.0f);
}

void FormatDoubleValue(const Row& r, char* buf, size_t bufSz) {
    const char* fmt = r.doubleFmt ? r.doubleFmt : "%.1f";
    _snprintf_s(buf, bufSz, _TRUNCATE, fmt, r.doublePtr ? *r.doublePtr : 0.0);
}

    // Compute each row's Y once per frame so render and hit-test agree.
struct RowRect {
    float y = 0.0f;
    float h = 0.0f;
    bool  visible = false;
};
// Stack-allocated per pass; must cover the largest builder (BuildPoolDelayRows, 224).
constexpr int kMaxListRows = 256;

    // Row rects; under scroll, visible rows intersect [contentTopY, contentBottomY].
// scroll offset, visible rows are those whose rect intersects
// [contentTopY, contentBottomY].
float ComputeRects(const ScreenLayout& layout, const Row* rows, int rowCount,
                   float scrollPx, RowRect* out) {
    float y = Scale::Snap(layout.contentTopY + layout.animOffsetY - Scale::Snap(scrollPx));
    for (int i = 0; i < rowCount; ++i) {
        RowRect& r = out[i];
        r.y = Scale::Snap(y);
        r.h = Scale::Snap(RowPixelHeight(rows[i], layout.contentW));
        const bool hidden = RowHidden(rows[i]);
        r.visible = !hidden &&
                    (r.y + r.h) > layout.contentTopY &&
                    r.y < layout.contentBottomY;
        if (!hidden) y += r.h;
    }
    return y - Scale::Snap(layout.contentTopY + layout.animOffsetY - Scale::Snap(scrollPx));
}

int FindFocusable(const Row* rows, int rowCount, int from, int dir) {
    if (from < 0) from = 0;
    if (from >= rowCount) from = rowCount - 1;
    for (int i = 0; i < rowCount; ++i) {
        int idx = from + dir * i;
        if (idx < 0) continue;
        if (idx >= rowCount) continue;
        if (RowHidden(rows[idx])) continue;
        if (RowIsFocusable(rows[idx])) return idx;
    }
    for (int i = 0; i < rowCount; ++i) {
        if (RowHidden(rows[i])) continue;
        if (RowIsFocusable(rows[i])) return i;
    }
    return -1;
}

constexpr int kMaxSubmenuDepth = 4;
constexpr float kSubmenuAnimMs = 130.0f;
constexpr float kSubmenuSlidePx = 36.0f;
constexpr float kPopupAnimMs = 100.0f;
constexpr float kPopupSlidePx = 24.0f;
using Anim::AnimT;
using Anim::EfzCosEase;

struct SubmenuFrame {
    const char* title = nullptr;
    RowListBuilder builder = nullptr;
    int focus = 0;
    ScrollState scroll;
};

struct SubmenuState {
    SubmenuFrame frames[kMaxSubmenuDepth];
    int depth = 0;
    DWORD animTick = 0;
    int animDir = 1;
};

SubmenuState g_submenus;
bool g_focusAboveRequested = false;

unsigned int g_popupMouseFrame = ~0u;
float g_popupLastMouseX = -1.0f;
float g_popupLastMouseY = -1.0f;
bool g_popupMouseMoved = false;

unsigned int g_listMouseFrame = ~0u;
float g_listLastMouseX = -1.0f;
float g_listLastMouseY = -1.0f;
bool g_listMouseMoved = false;

void ResetMouseTrackingState() {
    g_popupMouseFrame = ~0u;
    g_popupLastMouseX = -1.0f;
    g_popupLastMouseY = -1.0f;
    g_popupMouseMoved = false;

    g_listMouseFrame = ~0u;
    g_listLastMouseX = -1.0f;
    g_listLastMouseY = -1.0f;
    g_listMouseMoved = false;
}

float CurrentSubmenuOffsetX() {
    if (g_submenus.animTick == 0) return 0.0f;
    const float ease = EfzCosEase(AnimT(g_submenus.animTick, kSubmenuAnimMs));
    return Scale::Snap((1.0f - ease) * kSubmenuSlidePx * static_cast<float>(g_submenus.animDir));
}

void StartSubmenuAnimation(int dir) {
    g_submenus.animTick = Anim::NowTick();
    g_submenus.animDir = (dir < 0) ? -1 : 1;
}

void OpenSubmenu(const Row& r) {
    if (!r.submenuBuilder || g_submenus.depth >= kMaxSubmenuDepth) return;
    ResetMouseTrackingState();
    SubmenuFrame& f = g_submenus.frames[g_submenus.depth++];
    f.title = (r.submenuTitle && r.submenuTitle[0]) ? r.submenuTitle : r.label;
    f.builder = r.submenuBuilder;
    f.focus = 0;
    f.scroll = ScrollState{};
    StartSubmenuAnimation(+1);
    Sound::PlayDecision();
    Input::ResetEdges();
}

void CloseOneSubmenu() {
    if (g_submenus.depth <= 0) return;
    ResetMouseTrackingState();
    --g_submenus.depth;
    StartSubmenuAnimation(-1);
    Sound::PlayDecision();
    Input::ResetEdges();
}

ScreenLayout ApplySubmenuAnimation(const ScreenLayout& layout) {
    ScreenLayout out = layout;
    out.animOffsetX += CurrentSubmenuOffsetX();
    return out;
}

bool ActiveSubmenuRows(Row*& rows, int& rowCount, int*& focus, ScrollState*& scroll, const char*& title) {
    if (g_submenus.depth <= 0) return false;
    SubmenuFrame& f = g_submenus.frames[g_submenus.depth - 1];
    if (!f.builder) return false;
    rows = f.builder(rowCount);
    focus = &f.focus;
    scroll = &f.scroll;
    title = f.title;
    return true;
}

bool RowStartsInfoBlock(const Row* rows, int idx) {
    if (!rows || idx < 0) return false;
    if (RowHidden(rows[idx]) || rows[idx].kind != RowKind::Info) return false;
    if (idx == 0) return true;
    return RowHidden(rows[idx - 1]) || rows[idx - 1].kind != RowKind::Info;
}

void DrawInfoBlockBackgrounds(ImDrawList* dl, const ScreenLayout& layout,
                              const Row* rows, int rowCount,
                              const RowRect* rects) {
    if (!dl || !rows || !rects) return;
    using namespace Theme;
    const float x = Scale::Snap(layout.contentX + layout.animOffsetX);
    const float w = Scale::Snap(layout.contentW);

    for (int i = 0; i < rowCount; ++i) {
        if (!RowStartsInfoBlock(rows, i)) continue;

        int end = i;
        while (end + 1 < rowCount &&
               !RowHidden(rows[end + 1]) &&
               rows[end + 1].kind == RowKind::Info) {
            ++end;
        }

        const float y1 = Scale::Snap(rects[i].y);
        const float y2 = Scale::Snap(rects[end].y + rects[end].h);
        if (y2 <= layout.contentTopY || y1 >= layout.contentBottomY) {
            i = end;
            continue;
        }

        const float top = Scale::Snap(y1 + 1.0f);
        const float bot = Scale::Snap(y2 - 1.0f);
        dl->AddRectFilled(ImVec2(x - 2.0f, top),
                          ImVec2(x + w + 2.0f, bot),
                          kInfoFill);
        dl->AddRectFilled(ImVec2(x + 1.0f, top),
                          ImVec2(x + 5.0f, bot),
                          kInfoAccent);
        dl->AddLine(ImVec2(x - 2.0f, top),
                    ImVec2(x + w + 2.0f, top),
                    IM_COL32(255, 255, 255, 55), 1.0f);
        dl->AddLine(ImVec2(x - 2.0f, bot),
                    ImVec2(x + w + 2.0f, bot),
                    IM_COL32(255, 255, 255, 42), 1.0f);

        i = end;
    }
}

} // namespace

Row Header(const char* label) {
    Row r{};
    r.kind = RowKind::Header;
    r.label = label;
    return r;
}

Row Info(const char* label) {
    Row r{};
    r.kind = RowKind::Info;
    r.label = label;
    return r;
}

Row Spacer() {
    Row r{};
    r.kind = RowKind::Spacer;
    r.label = "";
    return r;
}

Row Custom(float height,
           RowCustomRenderer draw,
           bool (*isHidden)()) {
    Row r{};
    r.kind = RowKind::Custom;
    r.label = "";
    r.customHeight = height;
    r.customDraw = draw;
    r.isHidden = isHidden;
    return r;
}

Row Toggle(const char* label, bool* p,
           void (*onChange)(),
           bool (*isDisabled)(),
           bool (*isHidden)(),
           bool useSwitchPlayerToggle) {
    Row r{};
    r.kind = RowKind::Toggle;
    r.label = label;
    r.boolPtr = p;
    r.onChange = onChange;
    r.isDisabled = isDisabled;
    r.isHidden = isHidden;
    r.useSwitchPlayerToggle = useSwitchPlayerToggle;
    return r;
}

Row IntNum(const char* label, int* p, int mn, int mx,
           int stepSmall, int stepBig,
           void (*onChange)(),
           bool (*isDisabled)(),
           bool (*isHidden)()) {
    Row r{};
    r.kind = RowKind::IntNumber;
    r.label = label;
    r.intPtr = p;
    r.intMin = mn;
    r.intMax = mx;
    r.intStepSmall = stepSmall;
    r.intStepBig = stepBig;
    r.onChange = onChange;
    r.isDisabled = isDisabled;
    r.isHidden = isHidden;
    return r;
}

Row IntSlider(const char* label, int* p, int mn, int mx,
              int stepSmall, int stepBig,
              void (*onChange)(),
              bool (*isDisabled)(),
              bool (*isHidden)()) {
    Row r{};
    r.kind = RowKind::IntSlider;
    r.label = label;
    r.intPtr = p;
    r.intMin = mn;
    r.intMax = mx;
    r.intStepSmall = stepSmall;
    r.intStepBig = stepBig;
    r.onChange = onChange;
    r.isDisabled = isDisabled;
    r.isHidden = isHidden;
    return r;
}

Row FloatNum(const char* label, float* p, float mn, float mx,
             float stepSmall, float stepBig,
             const char* fmt,
             void (*onChange)(),
             bool (*isDisabled)(),
             bool (*isHidden)()) {
    Row r{};
    r.kind = RowKind::FloatNumber;
    r.label = label;
    r.floatPtr = p;
    r.floatMin = mn;
    r.floatMax = mx;
    r.floatStepSmall = stepSmall;
    r.floatStepBig = stepBig;
    r.floatFmt = fmt;
    r.onChange = onChange;
    r.isDisabled = isDisabled;
    r.isHidden = isHidden;
    return r;
}

Row DoubleNum(const char* label, double* p, double mn, double mx,
              double stepSmall, double stepBig,
              const char* fmt,
              void (*onChange)(),
              bool (*isDisabled)(),
              bool (*isHidden)()) {
    Row r{};
    r.kind = RowKind::DoubleNumber;
    r.label = label;
    r.doublePtr = p;
    r.doubleMin = mn;
    r.doubleMax = mx;
    r.doubleStepSmall = stepSmall;
    r.doubleStepBig = stepBig;
    r.doubleFmt = fmt;
    r.onChange = onChange;
    r.isDisabled = isDisabled;
    r.isHidden = isHidden;
    return r;
}

Row ChoicesRow(const char* label, int* idx, const char* const* items, int n,
               void (*onChange)(),
               bool (*isDisabled)(),
               bool (*isHidden)()) {
    Row r{};
    r.kind = RowKind::Choices;
    r.label = label;
    r.choiceIdxPtr = idx;
    r.choices = items;
    r.choiceCount = n;
    r.onChange = onChange;
    r.isDisabled = isDisabled;
    r.isHidden = isHidden;
    return r;
}

Row ActionStrengthRow(const char* label,
                      int* actionIdx, const char* const* actions, int actionCount,
                      int* strengthIdx, const char* const* strengths, int strengthCount,
                      RowValueFormatter formatter,
                      PairedChoiceChange onPrimaryChange,
                      PairedChoiceChange onSecondaryChange,
                      void (*onChange)(),
                      bool (*isDisabled)(),
                      bool (*isHidden)()) {
    Row r{};
    r.kind = RowKind::ActionStrength;
    r.label = label;
    r.choiceIdxPtr = actionIdx;
    r.choices = actions;
    r.choiceCount = actionCount;
    r.choice2IdxPtr = strengthIdx;
    r.choices2 = strengths;
    r.choice2Count = strengthCount;
    r.valueFormatter = formatter;
    r.onPrimaryChoiceChange = onPrimaryChange;
    r.onSecondaryChoiceChange = onSecondaryChange;
    r.onChange = onChange;
    r.isDisabled = isDisabled;
    r.isHidden = isHidden;
    return r;
}

Row TriggerButtonRow(const char* label,
                     int* action, int* strength,
                     int* dashFollowupMirror,
                     void (*onChange)(),
                     bool (*isDisabled)(),
                     bool (*isHidden)()) {
    Row r{};
    r.kind = RowKind::TriggerButton;
    r.label = label;
    r.choiceIdxPtr = action;
    r.choice2IdxPtr = strength;
    r.intPtr = dashFollowupMirror;
    r.valueFormatter = FormatTriggerButtonRow;
    r.onChange = onChange;
    r.isDisabled = isDisabled;
    r.isHidden = isHidden;
    return r;
}

Row DropdownRow(const char* label, int* idx, const char* const* items, int n,
                void (*onChange)(),
                bool (*isDisabled)(),
                bool (*isHidden)()) {
    Row r{};
    r.kind = RowKind::Dropdown;
    r.label = label;
    r.choiceIdxPtr = idx;
    r.choices = items;
    r.choiceCount = n;
    r.onChange = onChange;
    r.isDisabled = isDisabled;
    r.isHidden = isHidden;
    return r;
}

Row MaskPickerRow(const char* label, unsigned int* mask,
                  const char* const* items, int n,
                  void (*onChange)(),
                  bool (*isDisabled)(),
                  bool (*isHidden)()) {
    Row r{};
    r.kind = RowKind::MaskPicker;
    r.label = label;
    r.maskPtr = mask;
    r.choices = items;
    r.choiceCount = n;
    r.onChange = onChange;
    r.isDisabled = isDisabled;
    r.isHidden = isHidden;
    return r;
}

Row MaskPickerRow64(const char* label, uint64_t* maskLo, uint64_t* maskHi,
                    const char* const* items, int n,
                    void (*onChange)(),
                    bool (*isDisabled)(),
                    bool (*isHidden)()) {
    Row r{};
    r.kind = RowKind::MaskPicker;
    r.label = label;
    r.maskLoPtr = maskLo;
    r.maskHiPtr = maskHi;
    r.choices = items;
    r.choiceCount = n;
    r.onChange = onChange;
    r.isDisabled = isDisabled;
    r.isHidden = isHidden;
    return r;
}

Row Submenu(const char* label, const char* title, RowListBuilder builder,
            const char* (*valueFn)(),
            bool (*isDisabled)(),
            bool (*isHidden)()) {
    Row r{};
    r.kind = RowKind::Submenu;
    r.label = label;
    r.submenuTitle = title;
    r.submenuBuilder = builder;
    r.actionValue = valueFn;
    r.isDisabled = isDisabled;
    r.isHidden = isHidden;
    return r;
}

Row Action(const char* label, void (*fn)(),
           const char* (*valueFn)(),
           bool (*isDisabled)(),
           bool (*isHidden)()) {
    Row r{};
    r.kind = RowKind::Action;
    r.label = label;
    r.action = fn;
    r.actionValue = valueFn;
    r.isDisabled = isDisabled;
    r.isHidden = isHidden;
    return r;
}

    // Singleton: only one dropdown can be open at a time.
struct PopupState {
    bool active = false;
    int* choiceIdxPtr = nullptr;             // single-select target
    unsigned int* maskPtr = nullptr;         // multi-select target
    uint64_t* maskLoPtr = nullptr;           // 128-bit multi-select target, low bits
    uint64_t* maskHiPtr = nullptr;           // 128-bit multi-select target, high bits
    int* companionIdxPtr = nullptr;          // paired-choice secondary target
    const char* const* choices = nullptr;
    const int* choiceValueMap = nullptr;     // optional mapped target values for displayed choices
    int choiceCount = 0;
    const char* const* allChoices = nullptr; // source choices for categorized dropdowns
    int allChoiceCount = 0;
    const int* choiceCategoryMap = nullptr;
    const char* const* categoryChoices = nullptr;
    int categoryCount = 0;
    DWORD slideTick = 0;
    DWORD openTick = 0;
    int slideDir = 1;
    int selectedCategory = 0;
    const char* filteredChoices[128]{};
    int filteredChoiceValues[128]{};
    // Categorized list layout: rows include the group headers.
    const char* filteredHeaders[128]{};
    bool filteredIndented[128]{};
    int filteredRows[128]{};
    int filteredRowCount = 0;
    int filteredChoiceCount = 0;
    int focusIdx = 0;
    float scrollPx = 0.0f;
    Row sourceRow{};
    void (*onChange)() = nullptr;
    PairedChoiceChange onPrimaryChoiceChange = nullptr;
};
PopupState g_popup;

struct PopupGeom {
    float px, py, popupW, popupH;
    float listX, listY, listW, listH;
    float rowH;
};

inline bool PopupIsMulti() { return g_popup.maskPtr != nullptr || g_popup.maskLoPtr != nullptr; }
inline bool PopupUsesMappedChoiceValues() { return g_popup.choiceValueMap != nullptr; }
inline bool PopupIsCategorized() {
    return g_popup.categoryChoices != nullptr &&
           g_popup.choiceCategoryMap != nullptr &&
           g_popup.categoryCount > 0 &&
           g_popup.allChoices != nullptr &&
           g_popup.allChoiceCount > 0;
}
inline bool PopupShowsChecks() { return PopupIsMulti(); }

bool PopupMaskBitSet(int choiceValue) {
    if (choiceValue < 0 || choiceValue >= 128) return false;
    if (g_popup.maskLoPtr) {
        if (choiceValue < 64) {
            return (((*g_popup.maskLoPtr) >> choiceValue) & 1ull) != 0;
        }
        return g_popup.maskHiPtr &&
               (((*g_popup.maskHiPtr) >> (choiceValue - 64)) & 1ull) != 0;
    }
    return g_popup.maskPtr &&
           choiceValue < 32 &&
           (((*g_popup.maskPtr) >> choiceValue) & 1u) != 0;
}

void PopupToggleMaskBit(int choiceValue) {
    if (choiceValue < 0 || choiceValue >= 128) return;
    if (g_popup.maskLoPtr) {
        if (choiceValue < 64) {
            *g_popup.maskLoPtr ^= (1ull << choiceValue);
        } else if (g_popup.maskHiPtr) {
            *g_popup.maskHiPtr ^= (1ull << (choiceValue - 64));
        }
        return;
    }
    if (g_popup.maskPtr && choiceValue < 32) {
        *g_popup.maskPtr ^= (1u << choiceValue);
    }
}

bool RowMaskBitSet(const Row& r, int choiceValue) {
    if (choiceValue < 0 || choiceValue >= 128) return false;
    if (r.maskLoPtr) {
        if (choiceValue < 64) {
            return (((*r.maskLoPtr) >> choiceValue) & 1ull) != 0;
        }
        return r.maskHiPtr &&
               (((*r.maskHiPtr) >> (choiceValue - 64)) & 1ull) != 0;
    }
    return r.maskPtr &&
           choiceValue < 32 &&
           (((*r.maskPtr) >> choiceValue) & 1u) != 0;
}

int RowMaskPopcount(const Row& r) {
    int popcount = 0;
    if (r.maskLoPtr) {
        uint64_t lo = *r.maskLoPtr;
        uint64_t hi = r.maskHiPtr ? *r.maskHiPtr : 0;
        while (lo) { popcount += static_cast<int>(lo & 1ull); lo >>= 1; }
        while (hi) { popcount += static_cast<int>(hi & 1ull); hi >>= 1; }
    } else if (r.maskPtr) {
        unsigned int m = *r.maskPtr;
        while (m) { popcount += static_cast<int>(m & 1u); m >>= 1; }
    }
    return popcount;
}

bool RowUsesCategorizedChoices(const Row& r) {
    return r.categoryChoices != nullptr &&
           r.choiceCategoryMap != nullptr &&
           r.categoryCount > 0 &&
           r.choices != nullptr &&
           r.choiceCount > 0;
}

int ClampPopupIndex(int v, int maxExclusive) {
    if (maxExclusive <= 0) return 0;
    if (v < 0) return 0;
    if (v >= maxExclusive) return maxExclusive - 1;
    return v;
}

int PopupChoiceValueAt(int index) {
    if (index < 0 || index >= g_popup.choiceCount) return 0;
    return PopupUsesMappedChoiceValues() ? g_popup.choiceValueMap[index] : index;
}

bool RowChoiceVisible(const Row& r, int choiceValue) {
    return !r.choiceFilter || r.choiceFilter(r, choiceValue);
}

bool PopupSourceMatchesRow(const Row& row) {
    const Row& source = g_popup.sourceRow;
    if (row.maskLoPtr || row.maskHiPtr || row.maskPtr ||
        source.maskLoPtr || source.maskHiPtr || source.maskPtr) {
        return row.maskLoPtr == source.maskLoPtr &&
               row.maskHiPtr == source.maskHiPtr &&
               row.maskPtr == source.maskPtr;
    }
    if (row.choiceIdxPtr || source.choiceIdxPtr) {
        return row.choiceIdxPtr == source.choiceIdxPtr;
    }
    return TextEquals(CleanLabel(row.label), CleanLabel(source.label));
}

int PopupFocusedChoiceValueForRow(const Row& row) {
    if (!g_popup.active || !PopupSourceMatchesRow(row)) return -1;
    if (g_popup.focusIdx < 0 || g_popup.focusIdx >= g_popup.choiceCount) return -1;
    return PopupChoiceValueAt(g_popup.focusIdx);
}

const char* RowChoiceDisplayText(const Row& row, int choiceValue) {
    if (choiceValue < 0 || choiceValue >= row.choiceCount) return "";
    if (row.choiceValueFormatter) {
        const char* formatted = row.choiceValueFormatter(row, choiceValue);
        if (formatted && *formatted) return formatted;
    }
    return (row.choices && row.choices[choiceValue]) ? row.choices[choiceValue] : "";
}

constexpr int kMaskPreviewMaxVisible = 3;
constexpr int kMaskPreviewMaxSegments = kMaskPreviewMaxVisible * 2 + 1;

struct MaskPreviewToken {
    int choiceValue = -1;
    const char* text = "";
    bool highlighted = false;
};

struct MaskSelectionPreview {
    MaskPreviewToken tokens[kMaskPreviewMaxVisible];
    int tokenCount = 0;
    int selectedCount = 0;
    bool overflow = false;
    bool hasHighlightedToken = false;
};

int ClampMaskPreviewMaxVisible(int maxVisibleChoices) {
    if (maxVisibleChoices <= 0) return 1;
    if (maxVisibleChoices > kMaskPreviewMaxVisible) return kMaskPreviewMaxVisible;
    return maxVisibleChoices;
}

MaskSelectionPreview BuildMaskSelectionPreview(const Row& row, int maxVisibleChoices) {
    MaskSelectionPreview out{};
    maxVisibleChoices = ClampMaskPreviewMaxVisible(maxVisibleChoices);

    const int choiceLimit = (std::min)(row.choiceCount, 128);
    if (choiceLimit <= 0) return out;

    const int focusedChoice = PopupFocusedChoiceValueForRow(row);
    const bool focusedSelected = focusedChoice >= 0 &&
                                 focusedChoice < choiceLimit &&
                                 RowMaskBitSet(row, focusedChoice);

    for (int i = 0; i < choiceLimit; ++i) {
        if (!RowChoiceVisible(row, i)) continue;
        if (RowMaskBitSet(row, i)) ++out.selectedCount;
    }
    if (out.selectedCount <= 0) return out;

    out.overflow = out.selectedCount > maxVisibleChoices;
    if (out.overflow && focusedSelected) {
        out.tokens[out.tokenCount++] = {
            focusedChoice,
            RowChoiceDisplayText(row, focusedChoice),
            true
        };
        out.hasHighlightedToken = true;
    }

    for (int i = 0; i < choiceLimit && out.tokenCount < maxVisibleChoices; ++i) {
        if (!RowChoiceVisible(row, i)) continue;
        if (!RowMaskBitSet(row, i) || (out.overflow && focusedSelected && i == focusedChoice)) {
            continue;
        }
        const bool highlighted = focusedSelected && i == focusedChoice;
        out.tokens[out.tokenCount++] = {
            i,
            RowChoiceDisplayText(row, i),
            highlighted
        };
        if (highlighted) out.hasHighlightedToken = true;
    }

    return out;
}

void FormatMaskSelectionPreviewText(const MaskSelectionPreview& preview, char* buf, size_t bufSize) {
    if (!buf || bufSize == 0) return;
    if (preview.selectedCount <= 0 || preview.tokenCount <= 0) {
        strncpy_s(buf, bufSize, "EMPTY", _TRUNCATE);
        return;
    }

    buf[0] = '\0';
    for (int i = 0; i < preview.tokenCount; ++i) {
        if (i > 0) {
            strncat_s(buf, bufSize, ", ", _TRUNCATE);
        }
        const char* text = (preview.tokens[i].text && *preview.tokens[i].text)
                         ? preview.tokens[i].text
                         : "?";
        strncat_s(buf, bufSize, I18n::Tr(text), _TRUNCATE);
    }
    if (preview.overflow) {
        strncat_s(buf, bufSize, ", ...", _TRUNCATE);
    }
}

bool DrawMaskSelectionPreviewRow(ImDrawList* dl, float x, float y, float w,
                                 const Row& row, bool focused, bool disabled) {
    if (!LabelEquals(row.label, "ACTION POOL")) return false;

    const MaskSelectionPreview preview = BuildMaskSelectionPreview(row, 3);
    if (!preview.hasHighlightedToken) return false;

    Layout::TextSegment segments[kMaskPreviewMaxSegments]{};
    char segmentText[kMaskPreviewMaxSegments][48]{};
    int segmentCount = 0;

    auto addSegment = [&](const char* text, ImU32 col) {
        if (segmentCount >= kMaskPreviewMaxSegments || !text || !*text) return;
        strncpy_s(segmentText[segmentCount], sizeof(segmentText[segmentCount]), text, _TRUNCATE);
        segments[segmentCount] = { segmentText[segmentCount], col };
        ++segmentCount;
    };

    using namespace Theme;
    for (int i = 0; i < preview.tokenCount; ++i) {
        if (i > 0) addSegment(", ", kTextInactive);
        addSegment((preview.tokens[i].text && *preview.tokens[i].text) ? preview.tokens[i].text : "?",
                   preview.tokens[i].highlighted ? kTextActive : kTextInactive);
    }
    if (preview.overflow) {
        addSegment(", ", kTextInactive);
        addSegment("...", kTextInactive);
    }

    Layout::DrawRowDrillSegments(dl, x, y, w, row.label, segments, segmentCount, focused, disabled);
    return true;
}

const char* PopupChoiceDisplayText(int index) {
    if (index < 0 || index >= g_popup.choiceCount) return "";
    if (g_popup.sourceRow.choiceValueFormatter) {
        const char* formatted = g_popup.sourceRow.choiceValueFormatter(g_popup.sourceRow, PopupChoiceValueAt(index));
        if (formatted) return formatted;
    }
    return (g_popup.choices && g_popup.choices[index]) ? g_popup.choices[index] : "";
}

const char* PopupHelpText() {
    static char s_buf[240];
    if (!g_popup.active) return nullptr;
    if (PopupIsMulti() && g_popup.sourceRow.choiceHelpFormatter &&
        g_popup.focusIdx >= 0 && g_popup.focusIdx < g_popup.choiceCount) {
        const char* text = g_popup.sourceRow.choiceHelpFormatter(g_popup.sourceRow,
                                                                 PopupChoiceValueAt(g_popup.focusIdx));
        if (text && *text) return text;
    }
    if (!PopupIsMulti() && g_popup.sourceRow.choiceValueFormatter &&
        g_popup.focusIdx >= 0 && g_popup.focusIdx < g_popup.choiceCount) {
        const char* choice = PopupChoiceDisplayText(g_popup.focusIdx);
        _snprintf_s(s_buf, sizeof(s_buf), _TRUNCATE,
                    I18n::Tr("Select %s for this row."),
                    I18n::Tr((choice && *choice) ? choice : "this option"));
        return s_buf;
    }
    if (PopupIsMulti()) {
        return "Toggle each option in this list; selected entries are included in the setting.";
    }
    return "Choose one option from this list to apply it.";
}

int PopupColumnCount() {
    if (PopupIsCategorized()) return 1;
    if (g_popup.choiceCount >= 54) return 3;
    return g_popup.choiceCount >= 12 ? 2 : 1;
}

int PopupRowCount() {
    if (PopupIsCategorized()) return g_popup.filteredRowCount;
    const int columns = PopupColumnCount();
    return columns > 0 ? (g_popup.choiceCount + columns - 1) / columns : g_popup.choiceCount;
}

float PopupColumnGap() {
    return Scale::Snap(12.0f * Scale::Get().layoutScale);
}

float PopupHeaderHeight() {
    if (PopupIsCategorized()) return Scale::Snap(Scale::Get().tabBarHeight + 6.0f);
    ImFont* bFont = Layout::BodyFont();
    const float bPx = bFont ? bFont->FontSize : 13.0f;
    return Scale::Snap(bPx + 18.0f * Scale::Get().layoutScale);
}

int PopupIndexRow(int index) {
    if (PopupIsCategorized()) {
        return (index >= 0 && index < g_popup.choiceCount) ? g_popup.filteredRows[index] : 0;
    }
    const int rows = PopupRowCount();
    return rows > 0 ? index % rows : 0;
}

int PopupIndexColumn(int index) {
    const int rows = PopupRowCount();
    return rows > 0 ? index / rows : 0;
}

// A categorized header row resolves to the choice below it.
int PopupCellToIndex(int column, int row) {
    if (PopupIsCategorized()) {
        for (int i = 0; i < g_popup.choiceCount; ++i) {
            if (g_popup.filteredRows[i] >= row) return i;
        }
        return -1;
    }
    const int rows = PopupRowCount();
    if (rows <= 0) return -1;
    const int index = column * rows + row;
    return (index >= 0 && index < g_popup.choiceCount) ? index : -1;
}

float PopupColumnWidth(const PopupGeom& g) {
    const int columns = PopupColumnCount();
    if (columns <= 1) {
        return g.listW;
    }
    return (g.listW - PopupColumnGap() * static_cast<float>(columns - 1)) / static_cast<float>(columns);
}

bool PopupItemRect(const PopupGeom& g, int index, float& x0, float& y0, float& x1, float& y1) {
    const int row = PopupIndexRow(index);
    const int column = PopupIndexColumn(index);
    const float columnWidth = PopupColumnWidth(g);
    x0 = Scale::Snap(g.listX + static_cast<float>(column) * (columnWidth + PopupColumnGap()));
    y0 = Scale::Snap(g.listY + static_cast<float>(row) * g.rowH - Scale::Snap(g_popup.scrollPx));
    x1 = Scale::Snap(x0 + columnWidth);
    y1 = Scale::Snap(y0 + g.rowH);
    return !(y1 <= g.listY || y0 >= g.listY + g.listH);
}

int PopupIndexFromPoint(const PopupGeom& g, float x, float y) {
    if (x < g.listX || x > g.listX + g.listW || y < g.listY || y > g.listY + g.listH) {
        return -1;
    }

    const int columns = PopupColumnCount();
    const float columnWidth = PopupColumnWidth(g);
    const float stride = columnWidth + PopupColumnGap();
    int column = static_cast<int>((x - g.listX) / (stride > 0.0f ? stride : 1.0f));
    if (column < 0) column = 0;
    if (column >= columns) column = columns - 1;

    const float localX = x - (g.listX + static_cast<float>(column) * stride);
    if (localX < 0.0f || localX > columnWidth) {
        return -1;
    }

    const int row = static_cast<int>((y - g.listY + g_popup.scrollPx) / g.rowH);
    if (row < 0 || row >= PopupRowCount()) {
        return -1;
    }

    const int index = PopupCellToIndex(column, row);
    return (index >= 0 && PopupIndexRow(index) == row) ? index : -1;
}

void PopupMoveFocus(int rowDelta, int columnDelta) {
    if (g_popup.choiceCount <= 0) return;

    const int columns = PopupColumnCount();
    if (columns <= 1) {
        if (rowDelta < 0) {
            g_popup.focusIdx = (g_popup.focusIdx - 1 + g_popup.choiceCount) % g_popup.choiceCount;
        } else if (rowDelta > 0) {
            g_popup.focusIdx = (g_popup.focusIdx + 1) % g_popup.choiceCount;
        }
        return;
    }

    const int rows = PopupRowCount();
    int row = PopupIndexRow(g_popup.focusIdx);
    int column = PopupIndexColumn(g_popup.focusIdx);

    row = (row + rowDelta + rows) % rows;
    column = (column + columnDelta + columns) % columns;

    int next = PopupCellToIndex(column, row);
    while (next < 0 && row > 0) {
        --row;
        next = PopupCellToIndex(column, row);
    }
    if (next >= 0) {
        g_popup.focusIdx = next;
    }
}

int PopupCategoryForChoice(int choiceValue) {
    if (!PopupIsCategorized()) return 0;
    if (choiceValue < 0 || choiceValue >= g_popup.allChoiceCount) return 0;
    return ClampPopupIndex(g_popup.choiceCategoryMap[choiceValue], g_popup.categoryCount);
}

void PopupStartSlide(int direction) {
    g_popup.slideTick = Anim::NowTick();
    g_popup.slideDir = (direction < 0) ? -1 : 1;
}

bool PopupCategoryHasChoices(int category) {
    for (int i = 0; i < g_popup.allChoiceCount; ++i) {
        if (g_popup.choiceCategoryMap[i] == category && RowChoiceVisible(g_popup.sourceRow, i)) return true;
    }
    return false;
}

constexpr int kMaxPopupTabs = 12;
constexpr float kCategorizedPopupRows = 15.0f;

// Categories with nothing to pick get no tab.
int PopupTabs(const char** labels, int* categories) {
    int count = 0;
    for (int c = 0; c < g_popup.categoryCount && count < kMaxPopupTabs; ++c) {
        if (!PopupCategoryHasChoices(c)) continue;
        labels[count] = g_popup.categoryChoices[c];
        categories[count++] = c;
    }
    return count;
}

bool PopupSameGroup(const char* a, const char* b) {
    return a && b && std::strcmp(a, b) == 0;
}

// A lone entry gets no header: it would only repeat its own name.
void PopupLayoutGroups() {
    const RowChoiceValueFormatter groupOf = g_popup.sourceRow.choiceGroupFormatter;
    const int count = g_popup.filteredChoiceCount;
    const char* previous = nullptr;
    int row = 0;
    for (int i = 0; i < count; ++i) {
        const int value = g_popup.filteredChoiceValues[i];
        const char* group = groupOf ? groupOf(g_popup.sourceRow, value) : nullptr;
        const char* next = (groupOf && i + 1 < count)
            ? groupOf(g_popup.sourceRow, g_popup.filteredChoiceValues[i + 1]) : nullptr;
        const bool joinsPrevious = PopupSameGroup(group, previous);
        const bool joinsNext = PopupSameGroup(group, next);
        g_popup.filteredHeaders[i] = (!joinsPrevious && joinsNext) ? group : nullptr;
        g_popup.filteredIndented[i] = joinsPrevious || joinsNext;
        if (g_popup.filteredHeaders[i]) ++row;
        g_popup.filteredRows[i] = row++;
        previous = group;
    }
    g_popup.filteredRowCount = row;
}

// An empty category has no tab, so a request for one lands on the next non-empty one.
void PopupOpenCategory(int category) {
    if (!PopupIsCategorized()) return;
    category = ClampPopupIndex(category, g_popup.categoryCount);
    for (int step = 0; step < g_popup.categoryCount; ++step) {
        const int candidate = (category + step) % g_popup.categoryCount;
        if (PopupCategoryHasChoices(candidate)) {
            category = candidate;
            break;
        }
    }
    g_popup.selectedCategory = category;
    g_popup.filteredChoiceCount = 0;
    for (int i = 0; i < g_popup.allChoiceCount && g_popup.filteredChoiceCount < 128; ++i) {
        if (g_popup.choiceCategoryMap[i] != category) continue;
        if (!RowChoiceVisible(g_popup.sourceRow, i)) continue;
        const int local = g_popup.filteredChoiceCount++;
        g_popup.filteredChoices[local] = g_popup.allChoices[i];
        g_popup.filteredChoiceValues[local] = i;
    }
    PopupLayoutGroups();
    PopupStartSlide(+1);
    g_popup.choices = g_popup.filteredChoices;
    g_popup.choiceValueMap = g_popup.filteredChoiceValues;
    g_popup.choiceCount = g_popup.filteredChoiceCount;
    g_popup.focusIdx = 0;
    if (g_popup.choiceIdxPtr) {
        const int current = *g_popup.choiceIdxPtr;
        for (int i = 0; i < g_popup.filteredChoiceCount; ++i) {
            if (g_popup.filteredChoiceValues[i] == current) {
                g_popup.focusIdx = i;
                break;
            }
        }
    }
    g_popup.scrollPx = 0.0f;
}

bool PopupCycleCategory(int direction) {
    if (!PopupIsCategorized()) return false;
    for (int step = 1; step < g_popup.categoryCount; ++step) {
        const int candidate = (g_popup.selectedCategory + direction * step +
                               g_popup.categoryCount * step) % g_popup.categoryCount;
        if (!PopupCategoryHasChoices(candidate)) continue;
        PopupOpenCategory(candidate);
        PopupStartSlide(direction);
        return true;
    }
    return false;
}

void PopupStepHorizontal(int direction) {
    if (PopupCycleCategory(direction)) {
        Sound::PlayCursor();
    } else if (PopupColumnCount() > 1) {
        PopupMoveFocus(0, direction);
        Sound::PlayCursor();
    }
}

bool AdjustCategorizedRowChoice(const Row& r, int direction) {
    if (!r.choiceIdxPtr || direction == 0 || !RowUsesCategorizedChoices(r)) return false;
    const int current = ClampPopupIndex(*r.choiceIdxPtr, r.choiceCount);
    const int category = ClampPopupIndex(r.choiceCategoryMap[current], r.categoryCount);
    int matches[128];
    int count = 0;
    int currentLocal = -1;
    for (int i = 0; i < r.choiceCount && count < 128; ++i) {
        if (r.choiceCategoryMap[i] != category) continue;
        if (i == current) currentLocal = count;
        matches[count++] = i;
    }
    if (count <= 0) return false;
    if (currentLocal < 0) currentLocal = 0;
    currentLocal = (currentLocal + direction + count) % count;
    *r.choiceIdxPtr = matches[currentLocal];
    return true;
}

void OpenDropdownPopup(const Row& r) {
    if (!r.choiceIdxPtr || r.choiceCount <= 0 || !r.choices) return;
    ResetMouseTrackingState();
    g_popup.active = true;
    g_popup.choiceIdxPtr = r.choiceIdxPtr;
    g_popup.maskPtr = nullptr;
    g_popup.maskLoPtr = nullptr;
    g_popup.maskHiPtr = nullptr;
    g_popup.companionIdxPtr = (r.kind == RowKind::ActionStrength) ? r.choice2IdxPtr : nullptr;
    g_popup.sourceRow = r;
    g_popup.allChoices = nullptr;
    g_popup.allChoiceCount = 0;
    g_popup.choiceCategoryMap = nullptr;
    g_popup.categoryChoices = nullptr;
    g_popup.categoryCount = 0;
    g_popup.selectedCategory = 0;
    g_popup.filteredChoiceCount = 0;
    if (r.kind == RowKind::ActionStrength) {
        g_popup.choices = kGroupedActionChoices;
        g_popup.choiceValueMap = kGroupedActionValues;
        g_popup.choiceCount = kGroupedActionCount;
        g_popup.focusIdx = ActionToGroupedChoiceIndex(*r.choiceIdxPtr);
    } else if (RowUsesCategorizedChoices(r)) {
        g_popup.allChoices = r.choices;
        g_popup.allChoiceCount = r.choiceCount;
        g_popup.choiceCategoryMap = r.choiceCategoryMap;
        g_popup.categoryChoices = r.categoryChoices;
        g_popup.categoryCount = r.categoryCount;
        g_popup.selectedCategory = PopupCategoryForChoice(*r.choiceIdxPtr);
        PopupOpenCategory(g_popup.selectedCategory);
    } else {
        g_popup.choices = r.choices;
        g_popup.choiceValueMap = nullptr;
        g_popup.choiceCount = r.choiceCount;
        g_popup.focusIdx = *r.choiceIdxPtr;
    }
    g_popup.scrollPx = 0.0f;
    PopupStartSlide(+1);
    g_popup.openTick = g_popup.slideTick;
    g_popup.onChange = r.onChange;
    g_popup.onPrimaryChoiceChange = r.onPrimaryChoiceChange;
    Sound::PlayDecision();
}

void OpenMaskPopup(const Row& r) {
    if ((!r.maskPtr && !r.maskLoPtr) || r.choiceCount <= 0 || !r.choices) return;
    ResetMouseTrackingState();
    g_popup.active = true;
    g_popup.choiceIdxPtr = nullptr;
    g_popup.maskPtr = r.maskPtr;
    g_popup.maskLoPtr = r.maskLoPtr;
    g_popup.maskHiPtr = r.maskHiPtr;
    g_popup.companionIdxPtr = nullptr;
    g_popup.sourceRow = r;
    g_popup.allChoices = nullptr;
    g_popup.allChoiceCount = 0;
    g_popup.choiceCategoryMap = nullptr;
    g_popup.categoryChoices = nullptr;
    g_popup.categoryCount = 0;
    g_popup.selectedCategory = 0;
    g_popup.filteredChoiceCount = 0;
    if (RowUsesCategorizedChoices(r)) {
        g_popup.allChoices = r.choices;
        g_popup.allChoiceCount = r.choiceCount;
        g_popup.choiceCategoryMap = r.choiceCategoryMap;
        g_popup.categoryChoices = r.categoryChoices;
        g_popup.categoryCount = r.categoryCount;
        int focusCategory = 0;
        if (r.maskPtr || r.maskLoPtr) {
            for (int i = 0; i < r.choiceCount; ++i) {
                if (!RowMaskBitSet(r, i)) continue;
                if (!RowChoiceVisible(r, i)) continue;
                focusCategory = PopupCategoryForChoice(i);
                break;
            }
        }
        PopupOpenCategory(focusCategory);
    } else {
        if (r.choiceFilter) {
            g_popup.filteredChoiceCount = 0;
            for (int i = 0; i < r.choiceCount && g_popup.filteredChoiceCount < 128; ++i) {
                if (!RowChoiceVisible(r, i)) continue;
                const int local = g_popup.filteredChoiceCount++;
                g_popup.filteredChoices[local] = r.choices[i];
                g_popup.filteredChoiceValues[local] = i;
            }
            g_popup.choices = g_popup.filteredChoices;
            g_popup.choiceValueMap = g_popup.filteredChoiceValues;
            g_popup.choiceCount = g_popup.filteredChoiceCount;
        } else {
            g_popup.choices = r.choices;
            g_popup.choiceValueMap = nullptr;
            g_popup.choiceCount = r.choiceCount;
        }
        g_popup.focusIdx = 0;
    }
    g_popup.scrollPx = 0.0f;
    PopupStartSlide(+1);
    g_popup.openTick = g_popup.slideTick;
    g_popup.onChange = r.onChange;
    g_popup.onPrimaryChoiceChange = nullptr;
    Sound::PlayDecision();
}

void ClosePopup() {
    ResetMouseTrackingState();
    g_popup.active = false;
    g_popup.choiceIdxPtr = nullptr;
    g_popup.maskPtr = nullptr;
    g_popup.maskLoPtr = nullptr;
    g_popup.maskHiPtr = nullptr;
    g_popup.companionIdxPtr = nullptr;
    g_popup.choices = nullptr;
    g_popup.choiceValueMap = nullptr;
    g_popup.choiceCount = 0;
    g_popup.allChoices = nullptr;
    g_popup.allChoiceCount = 0;
    g_popup.choiceCategoryMap = nullptr;
    g_popup.categoryChoices = nullptr;
    g_popup.categoryCount = 0;
    g_popup.selectedCategory = 0;
    g_popup.filteredChoiceCount = 0;
    g_popup.focusIdx = 0;
    g_popup.scrollPx = 0.0f;
    g_popup.sourceRow = Row{};
    g_popup.onChange = nullptr;
    g_popup.onPrimaryChoiceChange = nullptr;
}

bool PopupActive() { return g_popup.active; }

void PopupEnsureFocusVisible(float viewportH) {
    const float rowH = Scale::Get().rowHeight;
    const float desiredY = static_cast<float>(PopupIndexRow(g_popup.focusIdx)) * rowH;
    const bool focusHasHeader = PopupIsCategorized() && g_popup.focusIdx >= 0 &&
                                g_popup.focusIdx < g_popup.choiceCount &&
                                g_popup.filteredHeaders[g_popup.focusIdx];
    const float topY = focusHasHeader ? desiredY - rowH : desiredY;
    if (topY < g_popup.scrollPx) {
        g_popup.scrollPx = topY;
    } else if (desiredY + rowH > g_popup.scrollPx + viewportH) {
        g_popup.scrollPx = desiredY + rowH - viewportH;
    }
    const float total = static_cast<float>(PopupRowCount()) * rowH;
    const float maxScroll = (total > viewportH) ? (total - viewportH) : 0.0f;
    if (g_popup.scrollPx < 0.0f) g_popup.scrollPx = 0.0f;
    if (g_popup.scrollPx > maxScroll) g_popup.scrollPx = maxScroll;
    g_popup.scrollPx = Scale::Snap(g_popup.scrollPx);
}

PopupGeom ComputePopupGeom(const ScreenLayout& layout) {
    using namespace Theme;
    const Scale::Metrics& metrics = Scale::Get();
    PopupGeom g{};
    g.rowH   = metrics.rowHeight;
    ImFont* bFont = Layout::BodyFont();
    const float bPx = bFont ? bFont->FontSize : 13.0f;
    const float headerHeight = PopupHeaderHeight();
    const float prefixW = Layout::MeasureTextW(bFont, bPx, PopupShowsChecks() ? "> [X] " : "> ");
    float widestChoiceW = 0.0f;
    for (int i = 0; i < g_popup.choiceCount && !PopupIsCategorized(); ++i) {
        const char* text = PopupChoiceDisplayText(i);
        widestChoiceW = (std::max)(widestChoiceW, Layout::MeasureTextW(bFont, bPx, text));
    }
    const int columns = PopupColumnCount();
    const float perColumnWidth = widestChoiceW + prefixW + Scale::Snap(28.0f * metrics.layoutScale);
    g.popupW = Scale::Snap((std::max)(320.0f * metrics.layoutScale,
                                      perColumnWidth * static_cast<float>(columns)
                                      + PopupColumnGap() * static_cast<float>(columns - 1)
                                      + 16.0f));
    const float maxW = kPanelW - 16.0f;
    if (g.popupW > maxW) g.popupW = maxW;
    const float maxH = Scale::Snap((layout.contentBottomY - layout.contentTopY) - 20.0f);
    const float desiredH = Scale::Snap(static_cast<float>(PopupRowCount()) * g.rowH + headerHeight + 8.0f);
    g.popupH = (desiredH < maxH) ? desiredH : maxH;
    // Fixed size, or the tab strip would move with each category's list.
    if (PopupIsCategorized()) {
        const char* tabLabels[kMaxPopupTabs];
        int tabCategories[kMaxPopupTabs];
        const int tabCount = PopupTabs(tabLabels, tabCategories);
        const float tabsW = Layout::TabBarWidth(tabLabels, tabCount) + 16.0f + Scale::Snap(32.0f * metrics.layoutScale);
        g.popupW = Scale::Snap((std::min)((std::max)(320.0f * metrics.layoutScale, tabsW), maxW));
        g.popupH = (std::min)(maxH, Scale::Snap(kCategorizedPopupRows * g.rowH + headerHeight + 8.0f));
    }
    g.px = Scale::Snap(layout.panelX + (kPanelW - g.popupW) * 0.5f);
    g.py = Scale::Snap(layout.contentTopY + ((layout.contentBottomY - layout.contentTopY) - g.popupH) * 0.5f);
    g.listX = Scale::Snap(g.px + 8.0f);
    g.listY = Scale::Snap(g.py + headerHeight);
    g.listW = Scale::Snap(g.popupW - 16.0f);
    g.listH = Scale::Snap(g.popupH - headerHeight - 8.0f);
    if (g.listH < g.rowH) g.listH = g.rowH;
    return g;
}

// Wheel scrolling carries focus along, or PopupEnsureFocusVisible scrolls straight back.
void PopupMoveFocusIntoView(const PopupGeom& g) {
    if (g.rowH <= 0.0f) return;
    const int firstRow = static_cast<int>(std::ceil(g_popup.scrollPx / g.rowH));
    const int lastRow = (std::max)(firstRow, static_cast<int>(std::floor((g_popup.scrollPx + g.listH) / g.rowH)) - 1);
    const int column = PopupIndexColumn(g_popup.focusIdx);
    const int row = (std::min)((std::max)(PopupIndexRow(g_popup.focusIdx), firstRow), lastRow);
    int next = PopupCellToIndex(column, row);
    if (next < 0 && column > 0) next = PopupCellToIndex(column - 1, row); // last column can be short
    if (next >= 0) g_popup.focusIdx = next;
}

    // Input-only pass: the popup must see edges BEFORE the list drains them.
void PopupTickInputOnly(const ScreenLayout& layout) {
    if (!g_popup.active) return;
    const PopupGeom g = ComputePopupGeom(layout);
    const bool isMulti = PopupIsMulti();
    const bool showChecks = PopupShowsChecks();

    auto toggleAt = [&](int i) -> bool {
        if (i < 0 || i >= g_popup.choiceCount) return false;
        if (isMulti) {
            const int choiceValue = PopupChoiceValueAt(i);
            PopupToggleMaskBit(choiceValue);
        } else {
            *g_popup.choiceIdxPtr = PopupUsesMappedChoiceValues() ? g_popup.choiceValueMap[i] : i;
            if (g_popup.onPrimaryChoiceChange) {
                g_popup.onPrimaryChoiceChange(g_popup.choiceIdxPtr, g_popup.companionIdxPtr);
            }
        }
        if (g_popup.onChange) g_popup.onChange();
        return !isMulti;
    };

    const bool navUp    = Input::NavUp();
    const bool navDown  = Input::NavDown();
    const bool navLeft  = Input::NavLeft();
    const bool navRight = Input::NavRight();
    const bool activate = Input::Activate();
    const bool back     = Input::Back();
    const int categoryStep = (Input::TopTabNext() || Input::SubTabNext()) ? 1
                           : (Input::TopTabPrev() || Input::SubTabPrev()) ? -1 : 0;
    const bool keyboardOrPadEdge = navUp || navDown || navLeft || navRight ||
                                   activate || back || categoryStep != 0 ||
                                   Input::SwitchPlayer();

    const unsigned int frame = ImGui::GetFrameCount();
    if (frame != g_popupMouseFrame) {
        g_popupMouseFrame = frame;
        g_popupMouseMoved = false;
        auto m = Input::GetMouse();
        if (m.valid) {
            if (g_popupLastMouseX < 0.0f && g_popupLastMouseY < 0.0f) {
                g_popupLastMouseX = m.x;
                g_popupLastMouseY = m.y;
            } else {
                const float dx = m.x - g_popupLastMouseX;
                const float dy = m.y - g_popupLastMouseY;
                if ((dx * dx + dy * dy) > 1.0f) {
                    g_popupMouseMoved = true;
                    g_popupLastMouseX = m.x;
                    g_popupLastMouseY = m.y;
                }
            }
        }
    }

    if (categoryStep != 0 && PopupCycleCategory(categoryStep)) Sound::PlayCursor();
    if (navUp)    { PopupMoveFocus(-1, 0); Sound::PlayCursor(); }
    if (navDown)  { PopupMoveFocus(+1, 0); Sound::PlayCursor(); }
    if (navLeft) PopupStepHorizontal(-1);
    if (navRight) PopupStepHorizontal(+1);
    if (activate) {
        const bool closeAfterPick = toggleAt(g_popup.focusIdx);
        Sound::PlayDecision();
        if (closeAfterPick) { ClosePopup(); return; }
    }
    if (back) {
        Sound::PlayDecision();
        ClosePopup();
        return;
    }

    const float wheel = Input::MouseWheel();
    if (wheel != 0.0f) {
        const float total = static_cast<float>(PopupRowCount()) * g.rowH;
        const float maxScroll = (total > g.listH) ? (total - g.listH) : 0.0f;
        g_popup.scrollPx = Scale::Snap((std::min)((std::max)(g_popup.scrollPx - wheel * g.rowH * 3.0f, 0.0f), maxScroll));
        PopupMoveFocusIntoView(g);
    }

    const bool mouseLeftEdge = Input::MouseLeftEdge();
    if (!keyboardOrPadEdge && mouseLeftEdge) {
        const auto mouse = Input::GetMouse();
        if (mouse.valid) {
            if (PopupIsCategorized() && mouse.y >= g.py && mouse.y < g.listY) {
                const char* tabLabels[kMaxPopupTabs];
                int tabCategories[kMaxPopupTabs];
                const int tabCount = PopupTabs(tabLabels, tabCategories);
                const int tab = Layout::TabBarIndexAt(g.listX, g.listW, tabLabels, tabCount, mouse.x);
                if (tab >= 0 && tabCategories[tab] != g_popup.selectedCategory) {
                    const int direction = tabCategories[tab] < g_popup.selectedCategory ? -1 : +1;
                    PopupOpenCategory(tabCategories[tab]);
                    PopupStartSlide(direction);
                    Sound::PlayCursor();
                }
            }
            const int hovered = PopupIndexFromPoint(g, mouse.x, mouse.y);
            if (hovered >= 0) {
                const bool closeAfterPick = toggleAt(hovered);
                Sound::PlayDecision();
                if (closeAfterPick) { ClosePopup(); return; }
            }
        }
        if (!Input::MouseHovering(g.px, g.py, g.popupW, g.popupH)) {
            Sound::PlayDecision();
            ClosePopup();
            return;
        }
    }
    if (!keyboardOrPadEdge && g_popupMouseMoved) {
        const auto mouse = Input::GetMouse();
        if (mouse.valid) {
            const int hovered = PopupIndexFromPoint(g, mouse.x, mouse.y);
            if (hovered >= 0) {
                g_popup.focusIdx = hovered;
            }
        }
    }

    PopupEnsureFocusVisible(g.listH);
}

void PopupRender(ImDrawList* dl, const ScreenLayout& layout) {
    if (!g_popup.active || !dl) return;
    using namespace Theme;
    const PopupGeom g = ComputePopupGeom(layout);
    const float slideT = AnimT(g_popup.slideTick, kPopupAnimMs);
    const float slideX = Scale::Snap((1.0f - EfzCosEase(slideT)) * kPopupSlidePx *
                                     static_cast<float>(g_popup.slideDir));
    const int popupFirstVertex = dl->VtxBuffer.Size;

    ImFont* bFont = Layout::BodyFont();
    const float bPx = bFont ? bFont->FontSize : 13.0f;

    dl->AddRectFilled(ImVec2(Scale::Snap(layout.panelX - 2.0f), Scale::Snap(layout.contentTopY - 2.0f)),
                      ImVec2(Scale::Snap(layout.panelX + kPanelW + 2.0f), Scale::Snap(layout.contentBottomY + 2.0f)),
                      IM_COL32(0, 0, 0, 160));
    dl->AddRectFilled(ImVec2(g.px, g.py), ImVec2(g.px + g.popupW, g.py + g.popupH), IM_COL32(0, 0, 0, 235));
    dl->AddRect      (ImVec2(g.px, g.py), ImVec2(g.px + g.popupW, g.py + g.popupH), kRule);

    const bool showChecks = PopupShowsChecks();
    const char* tabLabels[kMaxPopupTabs];
    int tabCategories[kMaxPopupTabs];
    const int tabCount = PopupTabs(tabLabels, tabCategories);
    if (tabCount > 0) {
        int activeTab = -1;
        for (int i = 0; i < tabCount; ++i) {
            if (tabCategories[i] == g_popup.selectedCategory) activeTab = i;
        }
        Layout::DrawTabBar(dl, g.listX, Scale::Snap(g.py + 2.0f), g.listW, tabLabels, tabCount, activeTab, activeTab);
    } else {
        Layout::DrawString(dl, bFont, bPx, g.px + 10.0f, Scale::Snap(g.py + 4.0f), kTextHeader,
                           PopupIsMulti() ? "MULTI-SELECT (ESC TO CLOSE)" : "SELECT");
    }
    dl->AddLine(ImVec2(Scale::Snap(g.px + 8.0f), Scale::Snap(g.listY - 4.0f)),
                ImVec2(Scale::Snap(g.px + g.popupW - 8.0f), Scale::Snap(g.listY - 4.0f)), kRule, 1.0f);

    const int listFirstVertex = dl->VtxBuffer.Size;
    dl->PushClipRect(ImVec2(g.listX, g.listY),
                     ImVec2(g.listX + g.listW, g.listY + g.listH),
                     true);
    for (int i = 0; i < g_popup.choiceCount; ++i) {
        float x0 = 0.0f;
        float y0 = 0.0f;
        float x1 = 0.0f;
        float y1 = 0.0f;
        const bool visible = PopupItemRect(g, i, x0, y0, x1, y1);
        x0 += slideX;
        x1 += slideX;
        const bool categorized = PopupIsCategorized();
        if (categorized && g_popup.filteredHeaders[i]) {
            Layout::DrawString(dl, bFont, bPx,
                               Scale::Snap(x0 + 4.0f),
                               Scale::Snap(y0 - g.rowH + (g.rowH - bPx) * 0.5f),
                               kTextHeader, g_popup.filteredHeaders[i]);
        }
        if (!visible) continue;
        const float indent = (categorized && g_popup.filteredIndented[i])
            ? Scale::Snap(12.0f * Scale::Get().layoutScale) : 0.0f;
        const bool focused = (i == g_popup.focusIdx);
        if (focused) {
            dl->AddRectFilled(ImVec2(x0, y0),
                              ImVec2(x1, y1),
                              kSelectedFill);
        }
        char line[256];
        // Translate before composing: the marker prefix would make the whole line miss the table.
        const char* choiceText = I18n::Tr(PopupChoiceDisplayText(i));
        if (showChecks) {
            const int choiceValue = PopupChoiceValueAt(i);
            const bool checked = PopupMaskBitSet(choiceValue);
            const char* mark = focused ? ">" : " ";
            _snprintf_s(line, sizeof(line), _TRUNCATE, "%s [%c] %s",
                        mark, checked ? 'X' : ' ', choiceText);
        } else {
            const char* mark = focused ? ">" : " ";
            _snprintf_s(line, sizeof(line), _TRUNCATE, "%s %s", mark, choiceText);
        }
        // popupW clamps to the panel, so a long entry can exceed its column; never into the next one.
        dl->PushClipRect(ImVec2(x0, y0), ImVec2(x1, y1), true);
        Layout::DrawString(dl, bFont, bPx,
                           Scale::Snap(x0 + 4.0f + indent),
                           Scale::Snap(y0 + (g.rowH - bPx) * 0.5f),
                           focused ? kTextActive : kTextInactive, line);
        dl->PopClipRect();
    }
    dl->PopClipRect();
    Anim::ScaleVertexAlpha(dl, listFirstVertex, slideT);

    const float total = static_cast<float>(PopupRowCount()) * g.rowH;
    if (total > g.listH) {
        const float barX = Scale::Snap(g.px + g.popupW - 4.0f);
        const float frac = g_popup.scrollPx / (total - g.listH);
        const float barH = Scale::Snap(g.listH * (g.listH / total));
        const float barY = Scale::Snap(g.listY + (g.listH - barH) * frac);
        dl->AddRectFilled(ImVec2(barX, barY),
                          ImVec2(barX + 2.0f, barY + barH),
                          kRule);
    }
    Anim::ScaleVertexAlpha(dl, popupFirstVertex, AnimT(g_popup.openTick, kPopupAnimMs));
}

void ClampFocus(const Row* rows, int rowCount, int& focus) {
    if (rowCount <= 0) { focus = -1; return; }
    if (focus < 0 || focus >= rowCount ||
        RowHidden(rows[focus]) || !RowIsFocusable(rows[focus])) {
        focus = FindFocusable(rows, rowCount, focus >= 0 ? focus : 0, +1);
    }
}

void EnsureFocusVisible(const ScreenLayout& layout,
                        const Row* rows, int rowCount,
                        int focus, ScrollState& scroll) {
    if (focus < 0 || focus >= rowCount) return;
    RowRect rects[kMaxListRows];
    if (rowCount > kMaxListRows) rowCount = kMaxListRows;
    if (focus >= rowCount) return;
    ComputeRects(layout, rows, rowCount, scroll.scrollPx, rects);

    const float viewTop = layout.contentTopY;
    const float viewBot = layout.contentBottomY;
    const float fy = rects[focus].y;
    const float fh = rects[focus].h;

    if (fy < viewTop) {
        scroll.scrollPx -= (viewTop - fy);
    } else if (fy + fh > viewBot) {
        scroll.scrollPx += (fy + fh) - viewBot;
    }

    float totalH = 0.0f;
    for (int i = 0; i < rowCount; ++i) {
        if (RowHidden(rows[i])) continue;
        totalH += RowPixelHeight(rows[i], layout.contentW);
    }
    const float viewH = viewBot - viewTop;
    scroll.maxScrollPx = (totalH > viewH) ? (totalH - viewH) : 0.0f;
    if (scroll.scrollPx < 0.0f) scroll.scrollPx = 0.0f;
    if (scroll.scrollPx > scroll.maxScrollPx) scroll.scrollPx = scroll.maxScrollPx;
    scroll.scrollPx = Scale::Snap(scroll.scrollPx);
    scroll.maxScrollPx = Scale::Snap(scroll.maxScrollPx);
}

void RenderList(ImDrawList* dl, const ScreenLayout& layout,
                const char* title,
                const Row* rows, int rowCount,
                int focus,
                const ScrollState& scroll) {
    using namespace Theme;

    ScreenLayout drawLayout = ApplySubmenuAnimation(layout);
    const Row* drawRows = rows;
    int drawCount = rowCount;
    int drawFocus = focus;
    const ScrollState* drawScroll = &scroll;
    (void)title;

    Row* submenuRows = nullptr;
    int* submenuFocus = nullptr;
    ScrollState* submenuScroll = nullptr;
    const char* submenuTitle = nullptr;
    if (ActiveSubmenuRows(submenuRows, drawCount, submenuFocus, submenuScroll, submenuTitle)) {
        drawRows = submenuRows;
        drawFocus = submenuFocus ? *submenuFocus : 0;
        drawScroll = submenuScroll ? submenuScroll : &scroll;
        (void)submenuTitle;
    }

    RowRect rects[kMaxListRows];
    if (drawCount > kMaxListRows) drawCount = kMaxListRows;
    ComputeRects(drawLayout, drawRows, drawCount, drawScroll->scrollPx, rects);
    SetCurrentHelpFromRow(drawRows, drawCount, drawFocus);

    ImFont* bFont = Layout::BodyFont();
    const float bPx = bFont ? bFont->FontSize : 13.0f;

    // Clip the scrollable region so partially scrolled rows don't bleed into header/hint.
    dl->PushClipRect(ImVec2(Scale::Snap(drawLayout.panelX), Scale::Snap(drawLayout.contentTopY)),
                     ImVec2(Scale::Snap(drawLayout.panelX + kPanelW), Scale::Snap(drawLayout.contentBottomY)),
                     true);

    DrawInfoBlockBackgrounds(dl, drawLayout, drawRows, drawCount, rects);

    for (int i = 0; i < drawCount; ++i) {
        if (!rects[i].visible) continue;
        const Row& r = drawRows[i];
        const bool focused = (i == drawFocus);
        const bool disabled = RowDisabled(r);
        const float x = Scale::Snap(drawLayout.contentX + drawLayout.animOffsetX);
        const float y = rects[i].y;
        const float w = Scale::Snap(drawLayout.contentW);

        switch (r.kind) {
            case RowKind::Header:
                Layout::DrawHeader(dl, x, y, w, r.label);
                break;
            case RowKind::Info: {
                const InfoWrapCacheEntry& wrapped = GetInfoWrapCacheEntry(r, w);
                const std::vector<std::string>& lines = wrapped.lines;
                const float rowH = rects[i].h;
                const float textBlockH = InfoTextBlockHeight(lines.size(), bPx);
                const float py0 = Scale::Snap(y + (std::max)(0.0f, (rowH - textBlockH) * 0.5f));

                // Subtle left-edge cursor when this Info is the focused row,
                // so keyboard scrolling has a visible anchor without making
                // body paragraphs noisy. Active text colour brightens too.
                if (focused) {
                    dl->AddRectFilled(
                        ImVec2(Scale::Snap(x + 2.0f), Scale::Snap(py0 - 1.0f)),
                        ImVec2(Scale::Snap(x + 4.0f), Scale::Snap(py0 + textBlockH + 1.0f)),
                        kTextActive);
                }
                const ImU32 textColor = focused ? kTextActive : kTextInactive;

                const float px = Scale::Snap(x + InfoTextX());
                for (size_t li = 0; li < lines.size(); ++li) {
                    const float py = Scale::Snap(py0 + static_cast<float>(li) * (bPx + InfoLineGap()));
                    Layout::DrawString(dl, bFont, bPx, px + 1.0f, py + 1.0f,
                                       IM_COL32(0, 0, 0, 190), lines[li].c_str());
                    Layout::DrawString(dl, bFont, bPx, px, py,
                                       textColor, lines[li].c_str());
                }
                break;
            }
            case RowKind::Spacer:
                break;
            case RowKind::Custom:
                if (r.customDraw) {
                    r.customDraw(dl, x, y, w, rects[i].h);
                }
                break;
            case RowKind::Toggle: {
                const bool v = r.boolPtr ? *r.boolPtr : false;
                Layout::DrawRowToggle(dl, x, y, w, r.label, v, focused, disabled);
                break;
            }
            case RowKind::IntNumber: {
                char buf[32];
                FormatIntValue(r, buf, sizeof(buf));
                Layout::DrawRowNumber(dl, x, y, w, r.label, buf, focused, disabled);
                break;
            }
            case RowKind::IntSlider: {
                char buf[32];
                const char* valueText = FormatIntDisplayValue(r, buf, sizeof(buf));
                Layout::DrawRowSlider(dl,
                                      x,
                                      y,
                                      w,
                                      r.label,
                                      valueText,
                                      GetIntSliderProgress01(r),
                                      focused,
                                      disabled);
                break;
            }
            case RowKind::FloatNumber: {
                char buf[32];
                FormatFloatValue(r, buf, sizeof(buf));
                Layout::DrawRowNumber(dl, x, y, w, r.label, buf, focused, disabled);
                break;
            }
            case RowKind::DoubleNumber: {
                char buf[32];
                FormatDoubleValue(r, buf, sizeof(buf));
                Layout::DrawRowNumber(dl, x, y, w, r.label, buf, focused, disabled);
                break;
            }
            case RowKind::Choices: {
                const int idx = (r.choiceIdxPtr ? *r.choiceIdxPtr : 0);
                Layout::DrawRowInlineChoices(dl, x, y, w, r.label,
                                             r.choices, r.choiceCount, idx,
                                             focused, disabled);
                break;
            }
            case RowKind::ActionStrength: {
                static char s_buf[96];
                const char* val = r.valueFormatter ? r.valueFormatter(r) : nullptr;
                if (!val) {
                    const int idx = r.choiceIdxPtr ? *r.choiceIdxPtr : 0;
                    const int idx2 = r.choice2IdxPtr ? *r.choice2IdxPtr : 0;
                    const char* primary = (r.choices && idx >= 0 && idx < r.choiceCount) ? r.choices[idx] : "?";
                    const char* secondary = (r.choices2 && idx2 >= 0 && idx2 < r.choice2Count) ? r.choices2[idx2] : "?";
                    _snprintf_s(s_buf, sizeof(s_buf), _TRUNCATE, "%s / %s", primary, secondary);
                    val = s_buf;
                }
                Layout::DrawRowDrill(dl, x, y, w, r.label, val, focused, disabled);
                break;
            }
            case RowKind::TriggerButton: {
                const char* val = r.valueFormatter ? r.valueFormatter(r) : "(NONE)";
                Layout::DrawRowLabelValue(dl, x, y, w, r.label, val, focused, disabled);
                break;
            }
            case RowKind::Dropdown: {
                const int idx = (r.choiceIdxPtr ? *r.choiceIdxPtr : 0);
                const char* val = r.valueFormatter ? r.valueFormatter(r) : nullptr;
                if (!val) {
                    val = (r.choices && idx >= 0 && idx < r.choiceCount)
                          ? r.choices[idx]
                          : "?";
                }
                Layout::DrawRowDrill(dl, x, y, w, r.label, val, focused, disabled);
                break;
            }
            case RowKind::MaskPicker: {
                if (DrawMaskSelectionPreviewRow(dl, x, y, w, r, focused, disabled)) {
                    break;
                }
                const char* val = r.valueFormatter ? r.valueFormatter(r) : nullptr;
                static char s_buf[24];
                if (!val) {
                    _snprintf_s(s_buf, sizeof(s_buf), _TRUNCATE,
                                "%d / %d", RowMaskPopcount(r), r.choiceCount);
                    val = s_buf;
                }
                Layout::DrawRowDrill(dl, x, y, w, r.label, val, focused, disabled);
                break;
            }
            case RowKind::Submenu: {
                const char* val = r.actionValue ? r.actionValue() : nullptr;
                Layout::DrawRowDrill(dl, x, y, w, r.label, val, focused, disabled);
                break;
            }
            case RowKind::Action: {
                const char* val = r.actionValue ? r.actionValue() : nullptr;
                Layout::DrawRowDrill(dl, x, y, w, r.label, val, focused, disabled);
                break;
            }
        }
    }

    dl->PopClipRect();

    if (drawScroll->maxScrollPx > 0.0f) {
        const float viewH = Scale::Snap(drawLayout.contentBottomY - drawLayout.contentTopY);
        const float totalH = viewH + drawScroll->maxScrollPx;
        const float barX = Scale::Snap(drawLayout.panelX + kPanelW - 4.0f);
        const float frac = drawScroll->scrollPx / drawScroll->maxScrollPx;
        const float barH = Scale::Snap(viewH * (viewH / totalH));
        const float barY = Scale::Snap(drawLayout.contentTopY + (viewH - barH) * frac);
        dl->AddRectFilled(ImVec2(barX, barY),
                          ImVec2(barX + 2.0f, barY + barH),
                          kRule);
    }
}

// Wheel scrolling carries focus along, or EnsureFocusVisible scrolls straight back.
void MoveFocusIntoView(const ScreenLayout& layout, const Row* rows, int rowCount,
                       const RowRect* rects, int& focus) {
    const auto inView = [&](int i) {
        return rects[i].visible && RowIsFocusable(rows[i]) &&
               rects[i].y >= layout.contentTopY &&
               rects[i].y + rects[i].h <= layout.contentBottomY;
    };
    if (focus < 0 || focus >= rowCount || inView(focus)) return;
    if (rects[focus].y < layout.contentTopY) {
        for (int i = focus + 1; i < rowCount; ++i) {
            if (inView(i)) { focus = i; return; }
        }
    } else {
        for (int i = focus - 1; i >= 0; --i) {
            if (inView(i)) { focus = i; return; }
        }
    }
}

bool HandleRowsInput(const ScreenLayout& layout,
                     const Row* rows, int rowCount,
                     int& focus,
                     ScrollState& scroll,
                     bool submenuContext) {
    if (IsKeybindActive()) {
        return false;
    }

    if (!layout.inputEnabled) {
        EnsureFocusVisible(layout, rows, rowCount, focus, scroll);
        return false;
    }

    // An open dropdown popup consumes input before the list drains edges: dispatch a
    // silent input-only pass here (the popup itself is drawn after the list, on top).
    if (PopupActive()) {
        PopupTickInputOnly(layout);
        return false;
    }

    RowRect rects[kMaxListRows];
    if (rowCount > kMaxListRows) rowCount = kMaxListRows;
    ComputeRects(layout, rows, rowCount, scroll.scrollPx, rects);

    ClampFocus(rows, rowCount, focus);

    const float wheel = Input::MouseWheel();
    if (wheel != 0.0f) {
        scroll.scrollPx = Scale::Snap(scroll.scrollPx - wheel * Scale::Get().rowHeight * 3.0f);
        if (scroll.scrollPx < 0.0f) scroll.scrollPx = 0.0f;
        if (scroll.scrollPx > scroll.maxScrollPx) scroll.scrollPx = scroll.maxScrollPx;
        ComputeRects(layout, rows, rowCount, scroll.scrollPx, rects);
        MoveFocusIntoView(layout, rows, rowCount, rects, focus);
    }

    const bool navUp    = Input::NavUp();
    const bool navDown  = Input::NavDown();
    const bool navLeft  = Input::NavLeft();
    const bool navRight = Input::NavRight();
    const bool keyboardOrPadEdge = navUp || navDown || navLeft || navRight ||
                                   Input::Activate() || Input::Back() ||
                                   Input::SwitchPlayer();

    // Hover retargets focus only when the mouse actually moved or on click,
    // so a stationary cursor cannot steal keyboard/gamepad focus.
    const unsigned int frame = ImGui::GetFrameCount();
    if (frame != g_listMouseFrame) {
        g_listMouseFrame = frame;
        g_listMouseMoved = false;
        auto m = Input::GetMouse();
        if (m.valid) {
            if (g_listLastMouseX < 0.0f && g_listLastMouseY < 0.0f) {
                g_listLastMouseX = m.x;
                g_listLastMouseY = m.y;
            } else {
                const float dx = m.x - g_listLastMouseX;
                const float dy = m.y - g_listLastMouseY;
                if ((dx * dx + dy * dy) > 1.0f) {
                    g_listMouseMoved = true;
                    g_listLastMouseX = m.x;
                    g_listLastMouseY = m.y;
                }
            }
        }
    }

    const bool mouseLeftEdge = Input::MouseLeftEdge();
    bool clickActivated = false;
    if (!keyboardOrPadEdge && (g_listMouseMoved || mouseLeftEdge)) {
        for (int i = 0; i < rowCount; ++i) {
            if (!rects[i].visible) continue;
            if (!RowIsFocusable(rows[i])) continue;
            if (Input::MouseHovering(Scale::Snap(layout.contentX + layout.animOffsetX), rects[i].y,
                                     layout.contentW, rects[i].h)) {
                focus = i;
                clickActivated = mouseLeftEdge;
                break;
            }
        }
    }

    const bool activate = Input::Activate() || clickActivated;

    const int firstFocusable = FindFocusable(rows, rowCount, 0, +1);
    const int oldFocus = focus;
    if (navUp && !submenuContext && focus == firstFocusable) {
        g_focusAboveRequested = true;
    } else if (navUp) {
        focus = FindFocusable(rows, rowCount, focus - 1, -1);
    }
    if (navDown) focus = FindFocusable(rows, rowCount, focus + 1, +1);
    if (focus != oldFocus) {
        Sound::PlayCursor();
    }

    if (focus >= 0 && focus < rowCount) {
        Row& r = const_cast<Row&>(rows[focus]);
        const bool disabled = RowDisabled(r);

        auto fire = [&](bool changed) {
            if (changed && r.onChange) r.onChange();
        };

        switch (r.kind) {
            case RowKind::Toggle: {
                if (disabled || !r.boolPtr) break;
                bool changed = false;
                const bool switchToggle = r.useSwitchPlayerToggle && Input::SwitchPlayer();
                if (activate || switchToggle)  { *r.boolPtr = !*r.boolPtr; changed = true; }
                else if (navLeft && *r.boolPtr)  { *r.boolPtr = false; changed = true; }
                else if (navRight && !*r.boolPtr){ *r.boolPtr = true;  changed = true; }
                if (changed) Sound::PlayCursor();
                fire(changed);
                break;
            }
            case RowKind::IntNumber:
            case RowKind::IntSlider: {
                if (disabled || !r.intPtr) break;
                bool changed = false;
                const int sBig = r.intStepBig > 0 ? r.intStepBig : r.intStepSmall;
                const int s = ShiftHeld() ? sBig : (r.intStepSmall > 0 ? r.intStepSmall : 1);
                if (navLeft)  { *r.intPtr -= s; changed = true; }
                if (navRight) { *r.intPtr += s; changed = true; }
                if (activate) { *r.intPtr += (s > 0 ? s : 1); changed = true; }
                if (*r.intPtr < r.intMin) *r.intPtr = r.intMin;
                if (*r.intPtr > r.intMax) *r.intPtr = r.intMax;
                if (changed) Sound::PlayCursor();
                fire(changed);
                break;
            }
            case RowKind::FloatNumber: {
                if (disabled || !r.floatPtr) break;
                bool changed = false;
                const float sBig = r.floatStepBig > 0.0f ? r.floatStepBig : r.floatStepSmall;
                const float s = ShiftHeld() ? sBig : r.floatStepSmall;
                if (navLeft)  { *r.floatPtr -= s; changed = true; }
                if (navRight) { *r.floatPtr += s; changed = true; }
                if (activate) { *r.floatPtr += s; changed = true; }
                if (*r.floatPtr < r.floatMin) *r.floatPtr = r.floatMin;
                if (*r.floatPtr > r.floatMax) *r.floatPtr = r.floatMax;
                if (changed) Sound::PlayCursor();
                fire(changed);
                break;
            }
            case RowKind::DoubleNumber: {
                if (disabled || !r.doublePtr) break;
                bool changed = false;
                const double sBig = r.doubleStepBig > 0.0 ? r.doubleStepBig : r.doubleStepSmall;
                const double s = ShiftHeld() ? sBig : r.doubleStepSmall;
                if (navLeft)  { *r.doublePtr -= s; changed = true; }
                if (navRight) { *r.doublePtr += s; changed = true; }
                if (activate) { *r.doublePtr += s; changed = true; }
                if (*r.doublePtr < r.doubleMin) *r.doublePtr = r.doubleMin;
                if (*r.doublePtr > r.doubleMax) *r.doublePtr = r.doubleMax;
                if (changed) Sound::PlayCursor();
                fire(changed);
                break;
            }
            case RowKind::Choices: {
                if (disabled || !r.choiceIdxPtr || r.choiceCount <= 0) break;
                int& idx = *r.choiceIdxPtr;
                bool changed = false;
                if (navLeft)              { idx = (idx - 1 + r.choiceCount) % r.choiceCount; changed = true; }
                else if (navRight || activate) { idx = (idx + 1) % r.choiceCount; changed = true; }
                if (changed) Sound::PlayCursor();
                fire(changed);
                break;
            }
            case RowKind::ActionStrength: {
                if (disabled) break;
                if (activate) {
                    OpenDropdownPopup(r);
                    break;
                }
                bool changed = false;
                const int secondaryCount = ActionStrengthSecondaryCount(r);
                const bool useSecondaryNav = r.choice2IdxPtr && secondaryCount > 0 && !ShiftHeld();
                if (useSecondaryNav && (navLeft || navRight)) {
                    int& idx = *r.choice2IdxPtr;
                    if (idx < 0 || idx >= secondaryCount) idx = 0;
                    idx = navLeft
                        ? (idx - 1 + secondaryCount) % secondaryCount
                        : (idx + 1) % secondaryCount;
                    if (r.onSecondaryChoiceChange) {
                        r.onSecondaryChoiceChange(r.choiceIdxPtr, r.choice2IdxPtr);
                    }
                    changed = true;
                } else if (r.choiceIdxPtr && r.choiceCount > 0 && (navLeft || navRight)) {
                    int& idx = *r.choiceIdxPtr;
                    idx = navLeft
                        ? (idx - 1 + r.choiceCount) % r.choiceCount
                        : (idx + 1) % r.choiceCount;
                    if (r.onPrimaryChoiceChange) {
                        r.onPrimaryChoiceChange(r.choiceIdxPtr, r.choice2IdxPtr);
                    }
                    changed = true;
                }
                if (changed) Sound::PlayCursor();
                fire(changed);
                break;
            }
            case RowKind::TriggerButton: {
                if (disabled) break;
                bool changed = false;
                if (navLeft) {
                    changed = AdjustTriggerButtonRow(r, -1);
                } else if (navRight || activate) {
                    changed = AdjustTriggerButtonRow(r, +1);
                }
                if (changed) Sound::PlayCursor();
                fire(changed);
                break;
            }
            case RowKind::Dropdown: {
                if (disabled) break;
                if (activate) { OpenDropdownPopup(r); break; }
                // L/R still cycles inline for quick tweaks
                if (!r.choiceIdxPtr || r.choiceCount <= 0) break;
                bool changed = false;
                if (r.inlineAdjuster) {
                    if (navLeft)  changed = r.inlineAdjuster(r, -1);
                    if (navRight) changed = r.inlineAdjuster(r, +1);
                } else if (RowUsesCategorizedChoices(r)) {
                    if (navLeft)  changed = AdjustCategorizedRowChoice(r, -1);
                    if (navRight) changed = AdjustCategorizedRowChoice(r, +1);
                } else {
                    int& idx = *r.choiceIdxPtr;
                    if (navLeft)  { idx = (idx - 1 + r.choiceCount) % r.choiceCount; changed = true; }
                    if (navRight) { idx = (idx + 1) % r.choiceCount; changed = true; }
                }
                if (changed) Sound::PlayCursor();
                fire(changed);
                break;
            }
            case RowKind::MaskPicker: {
                if (disabled) break;
                if (activate) { OpenMaskPopup(r); }
                break;
            }
            case RowKind::Submenu: {
                if (disabled) break;
                if (activate) { OpenSubmenu(r); }
                break;
            }
            case RowKind::Action: {
                if (disabled) break;
                if (activate && r.action) { Sound::PlayDecision(); r.action(); }
                break;
            }
            default:
                break;
        }
    }

    EnsureFocusVisible(layout, rows, rowCount, focus, scroll);

    const bool back = Input::Back();
    if (back && submenuContext) {
        CloseOneSubmenu();
        return false;
    }
    return back;
}

bool HandleListInput(const ScreenLayout& layout,
                     const Row* rows, int rowCount,
                     int& focus,
                     ScrollState& scroll) {
    g_focusAboveRequested = false;
    ScreenLayout activeLayout = ApplySubmenuAnimation(layout);
    Row* submenuRows = nullptr;
    int submenuCount = 0;
    int* submenuFocus = nullptr;
    ScrollState* submenuScroll = nullptr;
    const char* submenuTitle = nullptr;
    if (ActiveSubmenuRows(submenuRows, submenuCount, submenuFocus, submenuScroll, submenuTitle)) {
        if (!submenuFocus || !submenuScroll) return false;
        return HandleRowsInput(activeLayout, submenuRows, submenuCount, *submenuFocus, *submenuScroll, true);
    }
    return HandleRowsInput(activeLayout, rows, rowCount, focus, scroll, false);
}

bool IsPopupActive() { return PopupActive(); }
bool IsSubmenuActive() { return g_submenus.depth > 0; }

const char* CurrentHelpText() {
    return g_currentHelpText.empty() ? nullptr : g_currentHelpText.c_str();
}

const char* CurrentPopupHelpText() {
    return PopupHelpText();
}

const char* CurrentPopupHint() {
    if (PopupIsCategorized()) {
        return PopupIsMulti() ? "U/D MOVE   L/R TAB   ENTER TOGGLE   ESC CLOSE"
                              : "U/D MOVE   L/R TAB   ENTER SELECT   ESC CANCEL";
    }
    return PopupIsMulti() ? "UP/DOWN MOVE   ENTER TOGGLE   ESC CLOSE"
                          : "UP/DOWN MOVE   ENTER SELECT   ESC CANCEL";
}

void PlayContentSlide(int direction) {
    StartSubmenuAnimation(direction);
}

float CurrentSubmenuAlpha() {
    return AnimT(g_submenus.animTick, kSubmenuAnimMs);
}

int CurrentPopupFocusedChoiceValue(const Row* sourceRow) {
    return sourceRow ? PopupFocusedChoiceValueForRow(*sourceRow) : -1;
}

const char* FormatMaskSelectionSummary(const Row& row, int maxVisibleChoices) {
    static char s_buf[160];
    const MaskSelectionPreview preview = BuildMaskSelectionPreview(row, maxVisibleChoices);
    FormatMaskSelectionPreviewText(preview, s_buf, sizeof(s_buf));
    return s_buf;
}

const char* ActiveSubmenuTitle() {
    if (g_submenus.depth <= 0) return nullptr;
    return g_submenus.frames[g_submenus.depth - 1].title;
}

bool IsValuesPlayerEditorActive() {
    const char* title = ActiveSubmenuTitle();
    return title && strcmp(title, "PLAYER VALUES") == 0;
}

bool IsValuesContinuousRecoveryActive() {
    const char* title = ActiveSubmenuTitle();
    return title && strcmp(title, "CONTINUOUS RECOVERY") == 0;
}

bool IsValuesColumnEditorActive() {
    return IsValuesPlayerEditorActive() || IsValuesContinuousRecoveryActive();
}

void CloseTopSubmenu() {
    CloseOneSubmenu();
}

MenuNavigationRequest g_pendingMenuNav;
bool g_hasPendingMenuNav = false;

void RequestMenuNavigation(const MenuNavigationRequest& request) {
    g_pendingMenuNav = request;
    g_hasPendingMenuNav = true;
}

bool ConsumeMenuNavigation(MenuNavigationRequest& out) {
    if (!g_hasPendingMenuNav) return false;
    out = g_pendingMenuNav;
    g_hasPendingMenuNav = false;
    return true;
}

void OpenSubmenuDirect(RowListBuilder builder, const char* title, int focusRow) {
    if (!builder || g_submenus.depth >= kMaxSubmenuDepth) return;
    ResetMouseTrackingState();
    SubmenuFrame& f = g_submenus.frames[g_submenus.depth++];
    f.title = (title && title[0]) ? title : "SUBMENU";
    f.builder = builder;
    f.focus = (focusRow < 0) ? 0 : focusRow;
    f.scroll = ScrollState{};
    StartSubmenuAnimation(+1);
    Sound::PlayDecision();
    Input::ResetEdges();
}

bool ConsumeFocusAboveRequest() {
    const bool requested = g_focusAboveRequested;
    g_focusAboveRequested = false;
    return requested;
}

void ResetSubmenus() {
    g_submenus = SubmenuState{};
    ResetMouseTrackingState();
}

void ResetMouseTracking() {
    ResetMouseTrackingState();
}

bool TickPopupIfOpen(ImDrawList* dl, const ScreenLayout& layout) {
    if (!g_popup.active) return false;
    // Input was already processed in HandleListInput; this pass only draws the popup on top.
    PopupRender(dl, layout);
    return true;
}

namespace KeybindAPI { void TickInput(); }

constexpr uint32_t kKeybindLtBit = 0x10000u;
constexpr uint32_t kKeybindRtBit = 0x20000u;
constexpr int kKeybindTriggerThreshold = 30;

uint32_t PollRelevantGamepadMask() {
    XInputShim::RefreshSnapshotOncePerFrame();
    XInputShim::Snapshot snapshot{};
    XInputShim::CopySnapshot(snapshot);

    const int controllerIndex = Config::GetSettings().controllerIndex;
    uint32_t mask = 0;
    auto accumulate = [&](const XINPUT_STATE& state) {
        mask |= state.Gamepad.wButtons;
        if (state.Gamepad.bLeftTrigger > kKeybindTriggerThreshold) mask |= kKeybindLtBit;
        if (state.Gamepad.bRightTrigger > kKeybindTriggerThreshold) mask |= kKeybindRtBit;
    };

    if (controllerIndex >= 0 && controllerIndex <= 3) {
        if (snapshot.IsConnected(controllerIndex)) {
            accumulate(snapshot.states[controllerIndex]);
        }
        return mask;
    }

    for (int i = 0; i < 4; ++i) {
        if (snapshot.IsConnected(i)) {
            accumulate(snapshot.states[i]);
        }
    }
    return mask;
}

int FirstCapturedGamepadMask(uint32_t mask) {
    static const int kCapturePriority[] = {
        XINPUT_GAMEPAD_A,
        XINPUT_GAMEPAD_B,
        XINPUT_GAMEPAD_X,
        XINPUT_GAMEPAD_Y,
        XINPUT_GAMEPAD_LEFT_SHOULDER,
        XINPUT_GAMEPAD_RIGHT_SHOULDER,
        XINPUT_GAMEPAD_BACK,
        XINPUT_GAMEPAD_START,
        XINPUT_GAMEPAD_LEFT_THUMB,
        XINPUT_GAMEPAD_RIGHT_THUMB,
        XINPUT_GAMEPAD_DPAD_UP,
        XINPUT_GAMEPAD_DPAD_DOWN,
        XINPUT_GAMEPAD_DPAD_LEFT,
        XINPUT_GAMEPAD_DPAD_RIGHT,
        static_cast<int>(kKeybindLtBit),
        static_cast<int>(kKeybindRtBit),
    };

    for (int button : kCapturePriority) {
        if ((mask & static_cast<uint32_t>(button)) != 0) {
            return button;
        }
    }
    return 0;
}

void SnapshotKeyboardState(bool (&prevPressed)[256]) {
    for (int vk = 0; vk < 256; ++vk) {
        prevPressed[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
    }
}

bool KeyEdge(bool (&prevPressed)[256], int vk) {
    const bool now = (GetAsyncKeyState(vk) & 0x8000) != 0;
    const bool was = prevPressed[vk];
    prevPressed[vk] = now;
    return now && !was;
}

struct KeybindState {
    bool active = false;
    char title[48] = "";              // shown to user (e.g. "SAVE POSITION")
    int* settingsField = nullptr;     // mutable pointer into Config::Settings
    char iniSection[16] = "";         // INI section to persist into
    char iniKey[32]    = "";          // INI key to persist
    bool prevPressed[256] = {};
    uint32_t prevGamepadMask = 0;
    bool captureGamepad = false;
    bool disallowMenuReserved = false;
    bool primed = false; // first frame ignores the held key/button that opened binding
};
KeybindState g_keybind;

bool IsKeybindActive() { return g_keybind.active; }
bool IsGamepadKeybindActive() { return g_keybind.active && g_keybind.captureGamepad; }

void OpenKeybind(const char* title, int* field,
                 const char* section, const char* key,
                 bool disallowMenuReserved) {
    if (!field || !title || !section || !key) return;
    if (!Input::IsGameWindowActive()) return;

    g_keybind.active = true;
    strncpy_s(g_keybind.title, sizeof(g_keybind.title), title, _TRUNCATE);
    g_keybind.settingsField = field;
    strncpy_s(g_keybind.iniSection, sizeof(g_keybind.iniSection), section, _TRUNCATE);
    strncpy_s(g_keybind.iniKey, sizeof(g_keybind.iniKey), key, _TRUNCATE);
    g_keybind.captureGamepad = false;
    g_keybind.disallowMenuReserved = disallowMenuReserved;
    g_keybind.prevGamepadMask = 0;
    SnapshotKeyboardState(g_keybind.prevPressed);
    g_keybind.primed = false;
}

void OpenGamepadKeybind(const char* title, int* field,
                        const char* section, const char* key) {
    if (!field || !title || !section || !key) return;
    if (!Input::IsGameWindowActive()) return;

    g_keybind.active = true;
    strncpy_s(g_keybind.title, sizeof(g_keybind.title), title, _TRUNCATE);
    g_keybind.settingsField = field;
    strncpy_s(g_keybind.iniSection, sizeof(g_keybind.iniSection), section, _TRUNCATE);
    strncpy_s(g_keybind.iniKey, sizeof(g_keybind.iniKey), key, _TRUNCATE);
    g_keybind.captureGamepad = true;
    g_keybind.disallowMenuReserved = false;
    SnapshotKeyboardState(g_keybind.prevPressed);
    g_keybind.prevGamepadMask = PollRelevantGamepadMask();
    g_keybind.primed = false;
}

void CloseKeybind() {
    g_keybind.active = false;
    g_keybind.settingsField = nullptr;
    g_keybind.iniSection[0] = '\0';
    g_keybind.iniKey[0] = '\0';
    g_keybind.captureGamepad = false;
    g_keybind.disallowMenuReserved = false;
    g_keybind.prevGamepadMask = 0;
}

bool VkIsBindable(int vk) {
    // Disallow mouse buttons and pure modifiers; they'd interfere with menu navigation.
    if (vk >= 0x01 && vk <= 0x06) return false;     // mouse
    if (vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT) return false;
    if (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL) return false;
    if (vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU) return false;
    if (vk == VK_LWIN || vk == VK_RWIN) return false;
    if (vk == VK_CLEAR) return false;               // often reports as phantom "Num 5"
    if (vk == VK_ESCAPE) return false;              // reserved for cancel
    return true;
}

bool VkCountsTowardsPriming(int vk) {
    return VkIsBindable(vk);
}

// The menu key must not collide with key.ini game binds (menu nav) or any other hotkey.
bool MenuKeyCaptureAllowed(int vk) {
    if (!Config::IsMenuKeyAllowed(vk) || Config::IsVkBoundByEfz(vk)) return false;
    const KeyBindings& kb = detectedBindings;
    if (kb.directionsDetected &&
        (vk == kb.upKey || vk == kb.downKey || vk == kb.leftKey || vk == kb.rightKey)) return false;
    if (kb.attacksDetected &&
        (vk == kb.aButton || vk == kb.bButton || vk == kb.cButton || vk == kb.dButton)) return false;
    const Config::Settings& s = Config::GetSettings();
    // Effective keys as MonitorKeys resolves them, plus its hardwired '5'/'6'/F9.
    const auto eff = [](int vk, int fallback) { return Config::ResolveHotkey(vk, fallback); };
    const int others[] = {
        eff(s.teleportKey, '1'), eff(s.recordKey, '2'), s.toggleTitleKey, s.resetFrameCounterKey,
        eff(s.savestateSaveKey, 'U'), eff(s.savestateLoadKey, 'J'), eff(s.switchPlayersKey, 'L'),
        eff(s.macroRecordKey, 'I'), eff(s.macroPlayKey, 'O'), eff(s.macroSlotKey, 'K'),
        s.uiAcceptKey, s.uiRefreshKey, s.uiExitKey,
        eff(s.framestepPauseKey, VK_SPACE), eff(s.framestepStepKey, 'P'), s.swapCustomKey,
        '5', '6', VK_F9,
    };
    for (int other : others) if (vk == other) return false;
    return true;
}

bool VkAllowedForCurrentCapture(int vk) {
    if (!VkIsBindable(vk) || CollisionDisplay::IsGameHotkeyVk(vk)) return false;
    if (g_keybind.settingsField == &Config::GetSettings().configMenuKey) {
        return MenuKeyCaptureAllowed(vk);
    }
    if (vk == Config::GetSettings().configMenuKey) return false;
    if (g_keybind.disallowMenuReserved && (vk == VK_RETURN || vk == VK_SPACE)) {
        return false;
    }
    return true;
}

namespace KeybindAPI {
    void TickInput() {
        if (!g_keybind.active) return;
        if (!Input::IsGameWindowActive() || Input::IsSuspended()) {
            memset(g_keybind.prevPressed, 0, sizeof(g_keybind.prevPressed));
            g_keybind.prevGamepadMask = 0;
            g_keybind.primed = false;
            return;
        }

        if (KeyEdge(g_keybind.prevPressed, VK_ESCAPE)) {
            CloseKeybind();
            Input::ResetEdges();
            return;
        }

        if (g_keybind.captureGamepad) {
            const uint32_t currentMask = PollRelevantGamepadMask();
            const bool anyHeld = currentMask != 0;

            if (!g_keybind.primed) {
                g_keybind.prevGamepadMask = currentMask;
                if (!anyHeld) g_keybind.primed = true;
                return;
            }

            const uint32_t edgeMask = currentMask & ~g_keybind.prevGamepadMask;
            g_keybind.prevGamepadMask = currentMask;

            const int cancelMask = Config::GetSettings().gpToggleMenuButton;
            if (cancelMask >= 0 && (edgeMask & static_cast<uint32_t>(cancelMask)) != 0) {
                CloseKeybind();
                Input::ResetEdges();
                return;
            }

            if (KeyEdge(g_keybind.prevPressed, VK_DELETE) ||
                KeyEdge(g_keybind.prevPressed, VK_BACK)) {
                if (g_keybind.settingsField) {
                    *g_keybind.settingsField = -1;
                    Config::SetSetting(g_keybind.iniSection, g_keybind.iniKey, "-1");
                }
                CloseKeybind();
                Input::ResetEdges();
                return;
            }

            const int captured = FirstCapturedGamepadMask(edgeMask);
            if (captured && g_keybind.settingsField) {
                *g_keybind.settingsField = captured;
                Config::SetSetting(g_keybind.iniSection, g_keybind.iniKey,
                                   Config::GetGamepadButtonName(captured));
                CloseKeybind();
                Input::ResetEdges();
            }
            return;
        }

        bool anyHeld = false;
        int captured = 0;
        for (int vk = 0; vk < 256; ++vk) {
            if (vk == VK_ESCAPE) continue;
            if (!VkCountsTowardsPriming(vk)) continue;
            const bool now = (GetAsyncKeyState(vk) & 0x8000) != 0;
            const bool was = g_keybind.prevPressed[vk];
            g_keybind.prevPressed[vk] = now;
            if (now) anyHeld = true;
            if (g_keybind.primed && now && !was && captured == 0 && VkAllowedForCurrentCapture(vk)) {
                captured = vk;
            }
        }

        if (!g_keybind.primed) {
            // Only treat input as "real" once everything is released.
            if (!anyHeld) g_keybind.primed = true;
            return;
        }

        if (captured && g_keybind.settingsField) {
            *g_keybind.settingsField = captured;
            // Persist to INI as hex (Config::ParseKeyValue accepts hex/dec).
            char buf[16];
            _snprintf_s(buf, sizeof(buf), _TRUNCATE, "0x%X", captured);
            Config::SetSetting(g_keybind.iniSection, g_keybind.iniKey, buf);
            CloseKeybind();
            Input::ResetEdges();
        }
    }
} // namespace KeybindAPI

bool TickKeybindIfActive(ImDrawList* dl, const ScreenLayout& layout) {
    if (!g_keybind.active) return false;

    KeybindAPI::TickInput();
    if (!g_keybind.active) return true; // closed this frame

    using namespace Theme;
    const Scale::Metrics& metrics = Scale::Get();

    const float boxW = Scale::Snap(320.0f * metrics.layoutScale);
    const float boxH = Scale::Snap((g_keybind.captureGamepad ? 136.0f : 120.0f) * metrics.layoutScale);
    const float bx = Scale::Snap(layout.panelX + (kPanelW - boxW) * 0.5f);
    const float by = Scale::Snap(layout.contentTopY + ((layout.contentBottomY - layout.contentTopY) - boxH) * 0.5f);

    dl->AddRectFilled(ImVec2(Scale::Snap(layout.panelX - 2.0f), Scale::Snap(layout.contentTopY - 2.0f)),
                      ImVec2(Scale::Snap(layout.panelX + kPanelW + 2.0f), Scale::Snap(layout.contentBottomY + 2.0f)),
                      IM_COL32(0, 0, 0, 160));
    dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + boxW, by + boxH), kPanel);
    dl->AddRect      (ImVec2(bx, by), ImVec2(bx + boxW, by + boxH), kRule);

    ImFont* bFont = Layout::BodyFont();
    const float bPx = bFont ? bFont->FontSize : 13.0f;

    auto centerText = [&](float ty, const char* s, ImU32 col) {
        const float sw = Layout::MeasureTextW(bFont, bPx, s);
        Layout::DrawString(dl, bFont, bPx, Scale::Snap(bx + (boxW - sw) * 0.5f), ty, col, s);
    };

    centerText(Scale::Snap(by + 14.0f * metrics.layoutScale), g_keybind.title, kTextHeader);
    if (g_keybind.captureGamepad) {
        char cancelBuf[96];
        const int cancelMask = Config::GetSettings().gpToggleMenuButton;
        if (cancelMask >= 0) {
            _snprintf_s(cancelBuf, sizeof(cancelBuf), _TRUNCATE,
                        I18n::Tr("%s TO CANCEL"),
                        Config::GetGamepadButtonName(cancelMask).c_str());
        } else {
            _snprintf_s(cancelBuf, sizeof(cancelBuf), _TRUNCATE,
                        "ESC TO CANCEL");
        }
        if (!g_keybind.primed) {
            centerText(Scale::Snap(by + 46.0f * metrics.layoutScale), "RELEASE ALL INPUTS...", kTextInactive);
        } else {
            centerText(Scale::Snap(by + 46.0f * metrics.layoutScale), "PRESS A CONTROLLER BUTTON", kTextActive);
        }
        centerText(Scale::Snap(by + 74.0f * metrics.layoutScale), "DELETE / BACKSPACE TO DISABLE", kTextInactive);
        centerText(Scale::Snap(by + 100.0f * metrics.layoutScale), cancelBuf, kTextInactive);
    } else {
        if (!g_keybind.primed) {
            centerText(Scale::Snap(by + 50.0f * metrics.layoutScale), "RELEASE ALL KEYS...", kTextInactive);
        } else {
            centerText(Scale::Snap(by + 50.0f * metrics.layoutScale),
                       g_keybind.disallowMenuReserved
                           ? "PRESS A KEY (NO ENTER / SPACE)"
                           : "PRESS A KEY",
                       kTextActive);
        }
        centerText(Scale::Snap(by + 80.0f * metrics.layoutScale), "ESC TO CANCEL", kTextInactive);
    }

    return true;
}

} // namespace CustomMenu::Screens
