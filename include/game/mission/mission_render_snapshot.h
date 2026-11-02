#pragma once

#include "game/mission/mission_data.h"

namespace Mission::RenderSnapshot {

inline void CopyMissionView(const ::Mission::Mission& source,
                            ::Mission::Mission& destination) {
    destination = ::Mission::Mission{};
    destination.type = source.type;
    destination.name = source.name;
    destination.description = source.description;
    destination.player.character = source.player.character;
    destination.steps = source.steps;
    destination.strictEntityContacts = source.strictEntityContacts;
    destination.entityContacts = source.entityContacts;
    destination.entityLifecycles = source.entityLifecycles;
    destination.hints = source.hints;
}

}
