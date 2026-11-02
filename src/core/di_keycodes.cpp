#include "../../include/core/di_keycodes.h"
#include <string>

#ifndef VK_OEM_102
#define VK_OEM_102 0xE2
#endif

int MapDIKToVK(int dikCode) {
    for (const auto& mapping : KeyMappings) {
        if (static_cast<int>(mapping.dikCode) == dikCode) {
            return static_cast<int>(mapping.vkCode);
        }
    }

    if (dikCode == static_cast<int>(DIK_OEM_102)) {
        return VK_OEM_102;
    }

    UINT vk = MapVirtualKey(static_cast<UINT>(dikCode), MAPVK_VSC_TO_VK);
    if (vk != 0) {
        return static_cast<int>(vk);
    }

    return 0;
}

std::string GetDIKeyName(int dikCode) {
    for (const auto& mapping : KeyMappings) {
        if (static_cast<int>(mapping.dikCode) == dikCode) {
            return std::string(mapping.keyName);
        }
    }

    if (dikCode == static_cast<int>(DIK_OEM_102)) {
        return std::string("OEM102");
    }

    return "Key(" + std::to_string(dikCode) + ")";
}
