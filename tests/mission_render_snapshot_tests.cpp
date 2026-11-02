#include "game/mission/mission_render_snapshot.h"

#include <cstdio>
#include <stdexcept>

namespace {

void Check(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "mission render snapshot: %s\n", message);
        throw std::runtime_error(message);
    }
}

}

int main() try {
    ::Mission::Mission source;
    source.type = "mission";
    source.name = "Minagi immediate projectile";
    source.description = "render view";
    source.player.character = "minagi";
    source.player.meter = 1379;
    source.savestate.assign(4096, 'S');
    source.demo.assign(4096, 'D');
    source.hints.push_back("hint");

    ::Mission::Step step;
    step.notation = "236C";
    step.moveIds.push_back(255);
    source.steps.push_back(step);

    ::Mission::EntityContactRequirement contact;
    contact.notation = "236C (HIT)";
    contact.patterns.push_back(406);
    contact.semanticSourceAction = 0;
    contact.semanticSourceMove = 255;
    source.entityContacts.push_back(contact);
    source.strictEntityContacts = true;

    ::Mission::Mission snapshot;
    ::Mission::RenderSnapshot::CopyMissionView(source, snapshot);

    Check(snapshot.player.character == "minagi",
          "character-local entity identity was dropped");
    Check(snapshot.steps.size() == 1 &&
              snapshot.steps[0].moveIds.size() == 1 &&
              snapshot.steps[0].moveIds[0] == 255,
          "visible move recipe was not preserved");
    Check(snapshot.entityContacts.size() == 1 &&
              snapshot.entityContacts[0].patterns.size() == 1 &&
              snapshot.entityContacts[0].patterns[0] == 406 &&
              snapshot.entityContacts[0].semanticSourceAction == 0 &&
              snapshot.entityContacts[0].semanticSourceMove == 255,
          "entity presentation provenance was not preserved");
    Check(snapshot.savestate.empty() && snapshot.demo.empty(),
          "large runtime-only payload leaked into the per-frame view");
    const ::Mission::Mission defaults;
    Check(snapshot.player.meter == defaults.player.meter &&
              snapshot.player.meter != source.player.meter,
          "unused player state leaked into the per-frame view");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
