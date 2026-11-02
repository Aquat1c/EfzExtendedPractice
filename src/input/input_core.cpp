#include "../include/input/input_core.h"
#include "../include/core/memory.h"
#include "../include/core/logger.h"
#include "../include/core/constants.h"
#include "../include/utils/utilities.h"
#include "../include/input/input_buffer.h"
#include <sstream>
#include <cstring>

std::string DecodeInputMask(uint8_t inputMask) {
    if (inputMask == 0)
        return "N";
    std::string result;
    
    if (inputMask & GAME_INPUT_UP) {
        if (inputMask & GAME_INPUT_LEFT) result += "UL";
        else if (inputMask & GAME_INPUT_RIGHT) result += "UR";
        else result += "U";
    } else if (inputMask & GAME_INPUT_DOWN) {
        if (inputMask & GAME_INPUT_LEFT) result += "DL";
        else if (inputMask & GAME_INPUT_RIGHT) result += "DR";
        else result += "D";
    } else if (inputMask & GAME_INPUT_LEFT) {
        result += "L";
    } else if (inputMask & GAME_INPUT_RIGHT) {
        result += "R";
    }
    
    if (inputMask & GAME_INPUT_A) result += "A";
    if (inputMask & GAME_INPUT_B) result += "B";
    if (inputMask & GAME_INPUT_C) result += "C";
    if (inputMask & GAME_INPUT_D) result += "D";
    
    return result;
}

uintptr_t GetPlayerPointer(int playerNum) {
    uintptr_t base = GetEFZBase();
    if (!base) return 0;
    
    uintptr_t playerPtrOffset = (playerNum == 1) ? EFZ_BASE_OFFSET_P1 : EFZ_BASE_OFFSET_P2;
    uintptr_t playerPtr = 0;
    
    if (!SafeReadMemory(base + playerPtrOffset, &playerPtr, sizeof(uintptr_t))) {
        return 0;
    }
    
    return playerPtr;
}

bool WritePlayerInputImmediate(int playerNum, uint8_t inputMask) {
    return WritePlayerInputImmediateAt(GetPlayerPointer(playerNum), inputMask);
}

bool WritePlayerInputImmediateAt(uintptr_t playerPtr, uint8_t inputMask) {
    if (!playerPtr) return false;
    
    bool up = (inputMask & GAME_INPUT_UP) != 0;
    bool down = (inputMask & GAME_INPUT_DOWN) != 0;
    bool left = (inputMask & GAME_INPUT_LEFT) != 0;
    bool right = (inputMask & GAME_INPUT_RIGHT) != 0;
    
    uint8_t horzValue = 0;
    uint8_t vertValue = 0;
    
    // Horizontal: 0=neutral, 1=right, 255=left
    if (right) horzValue = 1;
    else if (left) horzValue = 255;
    
    // Vertical: 0=neutral, 1=down, 255=up
    if (down) vertValue = 1;
    else if (up) vertValue = 255;
    
    static_assert(INPUT_VERTICAL_OFFSET == INPUT_HORIZONTAL_OFFSET + 1 &&
                  INPUT_BUTTON_A_OFFSET == INPUT_HORIZONTAL_OFFSET + 2 &&
                  INPUT_BUTTON_B_OFFSET == INPUT_HORIZONTAL_OFFSET + 3 &&
                  INPUT_BUTTON_C_OFFSET == INPUT_HORIZONTAL_OFFSET + 4 &&
                  INPUT_BUTTON_D_OFFSET == INPUT_HORIZONTAL_OFFSET + 5,
                  "Immediate input registers must remain contiguous");
    const uint8_t staged[6] = {horzValue, vertValue,
        uint8_t((inputMask & GAME_INPUT_A) != 0), uint8_t((inputMask & GAME_INPUT_B) != 0),
        uint8_t((inputMask & GAME_INPUT_C) != 0), uint8_t((inputMask & GAME_INPUT_D) != 0)};
    // Raw 6-byte copy under SEH; fighter data is already writable and must not change page protections.
    __try {
        std::memcpy(reinterpret_cast<void*>(playerPtr + INPUT_HORIZONTAL_OFFSET), staged, sizeof(staged));
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool WritePlayerInputToBuffer(int playerNum, uint8_t inputMask) {
    uintptr_t playerPtr = GetPlayerPointer(playerNum);
    if (!playerPtr) return false;
    
    uint16_t currentIndex = 0;
    if (!SafeReadMemory(playerPtr + INPUT_BUFFER_INDEX_OFFSET, &currentIndex, sizeof(uint16_t)))
        return false;
    
    uint16_t bufferIndex = currentIndex % INPUT_BUFFER_SIZE;
    if (!SafeWriteMemory(playerPtr + INPUT_BUFFER_OFFSET + bufferIndex, &inputMask, sizeof(uint8_t)))
        return false;
    
    if (detailedLogging.load()) {
        LogOut("[BUFFER_WRITE] P" + std::to_string(playerNum) + " wrote " + DecodeInputMask(inputMask) + 
               " at index " + std::to_string(currentIndex) + " -> " + std::to_string(currentIndex + 1), true);
    }
    
    currentIndex = (currentIndex + 1) % INPUT_BUFFER_SIZE;
    if (!SafeWriteMemory(playerPtr + INPUT_BUFFER_INDEX_OFFSET, &currentIndex, sizeof(uint16_t)))
        return false;
    
    return true;
}

bool WritePlayerInput(int playerNum, uint8_t inputMask) {
    if (!WritePlayerInputImmediate(playerNum, inputMask))
        return false;
    
    if (!WritePlayerInputToBuffer(playerNum, inputMask))
        return false;
    
    return true;
}

// Rewind the index only; do not clear history bytes.
bool ResetPlayerInputBufferIndex(int playerNum) {
    uintptr_t playerPtr = GetPlayerPointer(playerNum);
    if (!playerPtr) return false;
    
    if (detailedLogging.load()) {
        DumpInputBuffer(playerNum, "BEFORE_INDEX_RESET");
    }
    
    uint16_t zero = 0;
    if (!SafeWriteMemory(playerPtr + INPUT_BUFFER_INDEX_OFFSET, &zero, sizeof(uint16_t))) {
        LogOut("[INPUT] Failed to reset buffer index for P" + std::to_string(playerNum), true);
        return false;
    }
    LogOut("[INPUT] Reset buffer index to 0 for P" + std::to_string(playerNum), true);
    
    if (detailedLogging.load()) {
        DumpInputBuffer(playerNum, "AFTER_INDEX_RESET");
    }
    
    return true;
}

uint16_t GetPlayerMoveID(int playerNum) {
    uintptr_t playerPtr = GetPlayerPointer(playerNum);
    if (!playerPtr) {
        LogOut("[BUFFER_DUMP] GetPlayerMoveID: null playerPtr for P" + std::to_string(playerNum), true);
        return 0;
    }
    uint16_t moveID = 0;
    if (!SafeReadMemory(playerPtr + MOVE_ID_OFFSET, &moveID, sizeof(uint16_t))) {
        LogOut("[BUFFER_DUMP] GetPlayerMoveID: failed to read MoveID for P" + std::to_string(playerNum), true);
        return 0;
    }
    return moveID;
}

void DumpInputBuffer(int playerNum, const std::string& context) {
    uintptr_t playerPtr = GetPlayerPointer(playerNum);
    if (!playerPtr) return;
    
    uint16_t bufferIndex = 0;
    if (!SafeReadMemory(playerPtr + INPUT_BUFFER_INDEX_OFFSET, &bufferIndex, sizeof(uint16_t))) {
        LogOut("[BUFFER_DUMP][" + context + "] Failed to read buffer index for P" + std::to_string(playerNum), true);
        return;
    }
    
    uint16_t moveID = GetPlayerMoveID(playerNum);
    
    std::stringstream ss;
    ss << "[BUFFER_DUMP][" << context << "] P" << playerNum 
       << " Index=" << bufferIndex 
    << " MoveID=" << moveID
       << " Last15=[";
    
    int startPos = (bufferIndex >= 15) ? (bufferIndex - 15) : 0;
    for (int i = startPos; i < bufferIndex; ++i) {
        uint8_t value = 0;
        if (SafeReadMemory(playerPtr + INPUT_BUFFER_OFFSET + i, &value, sizeof(uint8_t))) {
            if (i > startPos) ss << ",";
            ss << (int)i << ":" << DecodeInputMask(value);
        }
    }
    ss << "]";
    
    ss << " RecogWindow=[";
    int recogStart = (bufferIndex >= 5) ? (bufferIndex - 5) : 0;
    for (int i = recogStart; i < bufferIndex; ++i) {
        uint8_t value = 0;
        if (SafeReadMemory(playerPtr + INPUT_BUFFER_OFFSET + i, &value, sizeof(uint8_t))) {
            if (i > recogStart) ss << ",";
            ss << DecodeInputMask(value);
        }
    }
    ss << "]";
    
    LogOut(ss.str(), true);
}

static bool ClearPlayerInputBufferAt(uintptr_t playerPtr, int playerNum,
                                     bool resetIndex) {
    if (!playerPtr) return false;
    
    if (playerNum && detailedLogging.load()) {
        DumpInputBuffer(playerNum, "BEFORE_CLEAR");
    }
    
    std::vector<uint8_t> zeros(INPUT_BUFFER_SIZE, 0x00);
    bool ok = SafeWriteMemory(playerPtr + INPUT_BUFFER_OFFSET, zeros.data(), INPUT_BUFFER_SIZE);
    if (resetIndex) {
        uint16_t zero = 0;
        if (!SafeWriteMemory(playerPtr + INPUT_BUFFER_INDEX_OFFSET, &zero, sizeof(uint16_t))) {
            ok = false;
        }
    }
    if (playerNum) LogOut(std::string("[INPUT] Cleared input buffer for P") + std::to_string(playerNum) + (resetIndex?" (index reset)":""), true);
    return ok;
}

bool ClearPlayerInputBuffer(int playerNum, bool resetIndex) {
    return ClearPlayerInputBufferAt(GetPlayerPointer(playerNum), playerNum, resetIndex);
}

bool ClearPlayerCommandFlags(int playerNum) {
    const bool cleared = ClearPlayerCommandFlagsAt(GetPlayerPointer(playerNum));
    LogOut(std::string("[INPUT] Cleared command/dash flags for P") + std::to_string(playerNum), true);
    return cleared;
}

bool ClearPlayerCommandFlagsAt(uintptr_t playerPtr) {
    if (!playerPtr) return false;
    uint8_t zero = 0;
    bool ok1 = SafeWriteMemory(playerPtr + COMMAND_BUFFER_OFFSET, &zero, sizeof(uint8_t));
    bool ok2 = SafeWriteMemory(playerPtr + DASH_COMMAND_OFFSET, &zero, sizeof(uint8_t));
    return ok1 && ok2;
}

// The motion token is 16 bits; a one-byte write leaves a live high byte behind.
bool NeutralizeMotionToken(int playerNum) {
    uintptr_t playerPtr = GetPlayerPointer(playerNum);
    if (!playerPtr) return false;
    const uint16_t token = 99;
    bool ok = SafeWriteMemory(playerPtr + MOTION_TOKEN_OFFSET,
                              &token, sizeof(token));
    if (detailedLogging.load()) {
        LogOut(std::string("[INPUT] Neutralized motion token for P") + std::to_string(playerNum) +
               std::string(" -> 99 (word)"), true);
    }
    return ok;
}

bool FullCleanupAfterToggle(int playerNum) {
    const bool cleared = FullCleanupAfterToggleAt(GetPlayerPointer(playerNum));
    LogOut(std::string("[INPUT] Full cleanup after toggle for P") + std::to_string(playerNum), true);
    return cleared;
}

bool FullCleanupAfterToggleAt(uintptr_t playerPtr) {
    if (!playerPtr) return false;
    bool okAll = true;

    const uint16_t token = 99;
    okAll = SafeWriteMemory(playerPtr + MOTION_TOKEN_OFFSET, &token, sizeof(token)) && okAll;

    uint8_t zero = 0;
    okAll = SafeWriteMemory(playerPtr + INPUT_LATCH1_OFFSET, &zero, sizeof(uint8_t)) && okAll;
    okAll = SafeWriteMemory(playerPtr + INPUT_LATCH2_OFFSET, &zero, sizeof(uint8_t)) && okAll;
    okAll = SafeWriteMemory(playerPtr + DASH_TIMER_OFFSET, &zero, sizeof(uint8_t)) && okAll;

    okAll = ClearPlayerInputBufferAt(playerPtr, 0, true) && okAll;

    okAll = WritePlayerInputImmediateAt(playerPtr, GAME_INPUT_NEUTRAL) && okAll;

    // +400..412/+334/+336 are written by processCharacterInput itself; clear them or edges/direction go stale.
    const uint32_t zero32 = 0;
    const uint16_t zero16 = 0;
    constexpr uintptr_t heldOffsets[] = {400, 404, 408, 412};
    for (uintptr_t offset : heldOffsets) {
        okAll = SafeWriteMemory(playerPtr + offset, &zero32, sizeof(zero32)) &&
                okAll;
    }
    okAll = SafeWriteMemory(playerPtr + 334, &zero16, sizeof(zero16)) && okAll;
    okAll = SafeWriteMemory(playerPtr + 336, &zero32, sizeof(zero32)) && okAll;

    return okAll;
}
