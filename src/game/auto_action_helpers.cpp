#include "../include/game/auto_action.h"
#include "../include/core/constants.h"
#include "../include/utils/utilities.h"
#include "../include/core/logger.h"
#include "../include/input/input_motion.h"
#include "../include/game/auto_action_helpers.h"
#include "../include/core/memory.h"
#include "../include/game/game_state.h"
#include "../include/game/character_settings.h"
#include "../include/input/motion_constants.h"  
#include <vector>
#include <string>

int GetSpecialMoveStrength(int actionType, int triggerType) {
    switch (triggerType) {
        case TRIGGER_AFTER_BLOCK:
            return triggerAfterBlockStrength.load();
        
        case TRIGGER_ON_WAKEUP:
            return triggerOnWakeupStrength.load();
        
        case TRIGGER_AFTER_HITSTUN:
            return triggerAfterHitstunStrength.load();
        
        case TRIGGER_AFTER_AIRTECH:
            return triggerAfterAirtechStrength.load();
        case TRIGGER_ON_RG:
            return triggerOnRGStrength.load();
        
        default:
            return 1;
    }
}

std::string GetTriggerName(int triggerType) {
    switch (triggerType) {
        case TRIGGER_AFTER_BLOCK:
            return "After Block";
        case TRIGGER_ON_WAKEUP:
            return "On Wakeup";
        case TRIGGER_AFTER_HITSTUN:
            return "After Hitstun";
        case TRIGGER_AFTER_AIRTECH:
            return "After Airtech";
        case TRIGGER_ON_RG:
            return "On RG";
        default:
            return "Unknown Trigger";
    }
}

int ConvertTriggerActionToMotion(int actionType, int triggerType, int strengthOverride) {
    int strength = (strengthOverride >= 0) ? strengthOverride : GetSpecialMoveStrength(actionType, triggerType);

    switch (actionType) {
        case ACTION_5A: return MOTION_5A;
        case ACTION_5B: return MOTION_5B;
        case ACTION_5C: return MOTION_5C;
        case ACTION_5D: return MOTION_5D;
        case ACTION_2A: return MOTION_2A;
        case ACTION_2B: return MOTION_2B;
        case ACTION_2C: return MOTION_2C;
        case ACTION_2D: return MOTION_2D;
        case ACTION_JA: return MOTION_JA;
        case ACTION_JB: return MOTION_JB;
        case ACTION_JC: return MOTION_JC;
        case ACTION_JD: return MOTION_JD;
    case ACTION_6A: return MOTION_6A;
    case ACTION_6B: return MOTION_6B;
    case ACTION_6C: return MOTION_6C;
    case ACTION_6D: return MOTION_6D;
    case ACTION_4A: return MOTION_4A;
    case ACTION_4B: return MOTION_4B;
    case ACTION_4C: return MOTION_4C;
    case ACTION_4D: return MOTION_4D;
    case ACTION_1X:
        return strength == 1 ? MOTION_1B : strength == 2 ? MOTION_1C :
               strength == 3 ? MOTION_1D : MOTION_1A;
    case ACTION_3X:
        return strength == 1 ? MOTION_3B : strength == 2 ? MOTION_3C :
               strength == 3 ? MOTION_3D : MOTION_3A;
    case ACTION_J2X:
        return strength == 1 ? MOTION_J2B : strength == 2 ? MOTION_J2C :
               strength == 3 ? MOTION_J2D : MOTION_J2A;
    case ACTION_J6X:
        return strength == 1 ? MOTION_J6B : strength == 2 ? MOTION_J6C :
               strength == 3 ? MOTION_J6D : MOTION_J6A;

        case ACTION_QCF:
            if (strength == 1) return MOTION_236B;
            if (strength == 2) return MOTION_236C;
            if (strength == 3) return MOTION_236D;
            return MOTION_236A;

        case ACTION_DP:
            if (strength == 1) return MOTION_623B;
            if (strength == 2) return MOTION_623C;
            if (strength == 3) return MOTION_623D;
            return MOTION_623A;

        case ACTION_QCB:
            if (strength == 1) return MOTION_214B;
            if (strength == 2) return MOTION_214C;
            if (strength == 3) return MOTION_214D;
            return MOTION_214A;

        case ACTION_421:
            if (strength == 1) return MOTION_421B;
            if (strength == 2) return MOTION_421C;
            if (strength == 3) return MOTION_421D;
            return MOTION_421A;

        case ACTION_SUPER1:
            if (strength == 1) return MOTION_41236B;
            if (strength == 2) return MOTION_41236C;
            if (strength == 3) return MOTION_41236D;
            return MOTION_41236A;

        case ACTION_SUPER2:
            if (strength == 1) return MOTION_2141236B;
            if (strength == 2) return MOTION_2141236C;
            if (strength == 3) return MOTION_2141236D;
            return MOTION_2141236A;

        case ACTION_236236:
            if (strength == 1) return MOTION_236236B;
            if (strength == 2) return MOTION_236236C;
            if (strength == 3) return MOTION_236236D;
            return MOTION_236236A;

        case ACTION_214214:
            if (strength == 1) return MOTION_214214B;
            if (strength == 2) return MOTION_214214C;
            if (strength == 3) return MOTION_214214D;
            return MOTION_214214A;

        case ACTION_641236:
            if (strength == 1) return MOTION_641236B;
            if (strength == 2) return MOTION_641236C;
            if (strength == 3) return MOTION_641236D;
            return MOTION_641236A;

        case ACTION_463214:
            if (strength == 1) return MOTION_463214B;
            if (strength == 2) return MOTION_463214C;
            if (strength == 3) return MOTION_463214D;
            return MOTION_463214A;

        case ACTION_412:
            if (strength == 1) return MOTION_412B;
            if (strength == 2) return MOTION_412C;
            if (strength == 3) return MOTION_412D;
            return MOTION_412A;

        case ACTION_22:
            if (strength == 1) return MOTION_22B;
            if (strength == 2) return MOTION_22C;
            if (strength == 3) return MOTION_22D;
            return MOTION_22A;

        case ACTION_4123641236:
            if (strength == 1) return MOTION_4123641236B;
            if (strength == 2) return MOTION_4123641236C;
            if (strength == 3) return MOTION_4123641236D;
            return MOTION_4123641236A;

        case ACTION_6321463214:
            if (strength == 1) return MOTION_6321463214B;
            if (strength == 2) return MOTION_6321463214C;
            if (strength == 3) return MOTION_6321463214D;
            return MOTION_6321463214A;

    case ACTION_JUMP: return MOTION_NONE;
    case ACTION_BACKDASH: return MOTION_BACK_DASH;
    case ACTION_FORWARD_DASH: return MOTION_FORWARD_DASH;
    // Two-stage runtime recipe: native 44 then native 66 in Kaori backdash frames 4/5; must stay MOTION_NONE.
    case ACTION_KAORI_RECOIL_DUCK: return MOTION_NONE;
    case ACTION_66X:
    case ACTION_662X:
    case ACTION_664X:
        return MOTION_FORWARD_DASH;
        case ACTION_BLOCK: return MOTION_NONE;
    case ACTION_FINAL_MEMORY: return MOTION_NONE;

        default:
            LogOut("[AUTO-ACTION] WARNING: Unknown action type " + std::to_string(actionType) + 
                  ", defaulting to 5A", true);
            return MOTION_5A;
    }
}
