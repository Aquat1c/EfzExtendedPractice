#pragma once
#include <cstdint>
#include <cstring>
#include <string>

namespace Mission::EntityNames {

struct EntityNote { std::uint16_t pattern; const char* note; };
struct CharTable { const char* name; const EntityNote* entries; int count; };

enum class PresentationRole : std::uint8_t {
    InlineProjectile = 0,
    Setplay,
    Trap,
    Summon,
};

enum class LifecycleDisposition : std::uint8_t {
    ContactEffect = 0,
    Controller,
    PostContactRecovery,
    VisualEffect,
    DormantOrOrphan,
    Unresolved,
};

struct SemanticNote {
    const char* character;
    std::uint16_t pattern;
    const char* family;
    const char* label;
    PresentationRole role;
    LifecycleDisposition disposition;
    int producerMove;

    constexpr SemanticNote(const char* characterValue,
                           std::uint16_t patternValue,
                           const char* familyValue,
                           const char* labelValue,
                           PresentationRole roleValue,
                           LifecycleDisposition dispositionValue =
                               LifecycleDisposition::ContactEffect,
                           int producerMoveValue = -1)
        : character(characterValue), pattern(patternValue),
          family(familyValue), label(labelValue), role(roleValue),
          disposition(dispositionValue), producerMove(producerMoveValue) {}
};

static const EntityNote kAkane[] = {
    {400, "Assault/Trick Leap (needle)"},
    {401, "Assault/Trick Leap (needle)"},
    {402, "Assault/Trick Leap (needle phase)"},
    {403, "Assault/Trick Leap (internal phase)"},
    {404, "Assault/Trick Leap (cleanup helper)"},
    {405, "236236A/B/C (launched sword)"},
    {406, "236236A/B/C (falling sword variant)"},
    {407, "236236A/B/C (falling sword variant)"},
    {408, "236236A/B/C (falling sword variant)"},
    {409, "236C (Shiiko scooter attack)"},
    {410, "236A/B/C (Shiiko exit)"},
    {411, "236C (Shiiko effect helper)"},
    {412, "214214A/B/C"},
    {413, "236A/B/C (Shiiko exit)"},
    {414, "236A (Shiiko attack)"},
    {415, "236B (Shiiko controller)"},
    {416, "236B (Shiiko hit)"},
    {417, "641236A"},
    {418, "641236B"},
    {419, "641236C"},
    {420, "641236C (helper)"},
    {421, "FM (attack phase)"},
    {422, "FM (attack phase)"},
    {423, "FM (attack phase)"},
    {424, "FM (attack phase)"},
    {425, "FM (attack phase)"},
    {426, "FM (attack phase)"},
    {427, "FM (attack phase)"},
    {428, "FM (controller)"},
};

static const EntityNote kAkiko[] = {
    {400, "236A/B/C (refrigerator helper)"},
    {401, "236A (food recovery helper)"},
    {402, "236A (Sardine)"},
    {403, "236A (Egg)"},
    {404, "236A (Carrot)"},
    {405, "236B/236C (food recovery helper)"},
    {406, "236B/236C (Tuna)"},
    {407, "236B/236C (Radish)"},
    {408, "236B/236C (Durian)"},
    {409, "236B/236C (Durian later hit)"},
    {413, "41236A/B/C (successful-hit phase)"},
    {414, "41236A/B/C (successful-hit phase)"},
    {415, "214A (controller)"},
    {416, "214A (hit)"},
    {417, "214A/B/C (cleanup helper)"},
    {418, "214B (controller)"},
    {419, "214B (hit)"},
    {420, "214C (controller)"},
    {421, "214C (hit)"},
    {422, "236236A/B/C (vehicle controller)"},
    {427, "236236A/B/C (internal helper)"},
    {428, "236236A/B/C (internal helper)"},
    {429, "236236A/B/C (internal helper)"},
    {430, "236236A/B/C (helper) / 236B/236C (food debris)"},
    {431, "236236A/B/C (helper) / 236B/236C (food debris)"},
    {432, "236236A/B/C (internal helper)"},
    {433, "236236A/B/C (internal helper)"},
    {434, "236B/236C (food debris)"},
};

static const EntityNote kAyu[] = {
    {400, "623A/B (rising feather)"},
    {405, "(j.)214214A"},
    {406, "641236B (helper)"},
    {407, "641236C (helper)"},
    {408, "623C (rising feather)"},
    {409, "j.623C (downward feather)"},
    {410, "j.623C (ground explosion)"},
    {411, "623A/B/C (impact, exact version unresolved)"},
    {412, "623A/B/C (impact, exact version unresolved)"},
    {413, "623A/B/C (impact, exact version unresolved)"},
    {414, "FM (controller)"},
    {415, "FM (attack phase)"},
    {416, "FM (attack phase)"},
    {417, "FM (helper)"},
    {418, "236236A/B/C (helper)"},
    {419, "(j.)214214B"},
    {420, "(j.)214214C"},
    {421, "641236A (helper)"},
    {422, "623A/B/C (terminal phase, decomp-only)"},
    {423, "623A/B/C (terminal phase, decomp-only)"},
};

static const EntityNote kExnanase[] = {
    {401, "214A"},
    {402, "214B"},
    {403, "214C"},
    {404, "623A (visual helper)"},
    {405, "623B (visual helper)"},
    {406, "623C (visual helper)"},
    {412, "214214A"},
    {413, "214214B"},
    {414, "214214C"},
    {415, "214214A/B/C (helper)"},
};

static const EntityNote kIkumi[] = {
    {400, "623A/B/C (Bloodstained Cross helper)"},
    {401, "236A/B/C / 214214A (Red Line helper)"},
    {402, "214A/B/C (setup controller)"},
    {403, "214 (low blood, attack phase)"},
    {406, "214 (low blood, attack phase)"},
    {410, "214 (mid blood, attack phase)"},
    {411, "214 (mid blood, attack phase)"},
    {412, "214 (high blood, attack phase)"},
    {413, "214 (high blood, attack phase)"},
    {420, "214214A (clone attack)"},
    {421, "214214B (clone attack)"},
    {422, "214214C (clone attack)"},
    {423, "2141236A/B/C (blood-bomb controller)"},
    {424, "2141236A/B/C (detonation, exact version unresolved)"},
    {425, "2141236A/B/C (detonation, exact version unresolved)"},
    {426, "2141236A/B/C (detonation, exact version unresolved)"},
    {427, "2141236A/B/C (detonation, exact version unresolved)"},
    {428, "214214C (upgraded clone attack)"},
    {429, "214214C (upgraded clone attack)"},
    {430, "214214A/B/C (terminal attack)"},
    {431, "FM (on-hit internal phase)"},
    {432, "FM (on-hit attack phase)"},
    {433, "FM (on-hit attack phase)"},
    {434, "FM (on-hit attack phase)"},
    {435, "FM (cinematic helper)"},
    {436, "FM (cinematic helper)"},
    {437, "FM (cinematic helper)"},
    {438, "FM (cinematic helper)"},
    {439, "FM (on-hit attack phase)"},
    {478, "214 (retirement helper) / throw-super internal phase"},
};

static const EntityNote kKanna[] = {
    {400, "214A (Stone setup)"},
    {401, "214B (Stone setup)"},
    {402, "214C (Stone setup)"},
    {403, "214A/B/C (Stone eruption hit)"},
    {404, "236A/B (visual helper)"},
    {405, "41236A/B (launch helper)"},
    {406, "41236A/B (projectile)"},
    {415, "236236A (controller)"},
    {416, "236236A (helper)"},
    {417, "236236A (feather hit)"},
    {418, "ground 236236B (helper)"},
    {426, "j.236236B"},
    {427, "236C (visual helper)"},
    {429, "41236C (controller)"},
    {430, "41236C (hit)"},
    {431, "41236C (terminal phase)"},
    {432, "236236C (cinematic helper)"},
    {433, "236236C (cinematic helper)"},
    {434, "236236C (cinematic helper)"},
    {435, "236236C (cinematic helper)"},
    {436, "236236C (cinematic helper)"},
    {437, "236236C (cinematic helper)"},
    {438, "236236C (cinematic helper)"},
    {439, "236236C (cinematic helper)"},
    {440, "236236C (cinematic helper)"},
    {441, "623A/B/C (visual helper)"},
    {448, "236236C (cinematic helper)"},
};

static const EntityNote kKano[] = {
    {400, "236A/B/C (Lightning Bolt hit)"},
    {401, "214A/B/C (Fire Bolt travel hit)"},
    {402, "PAT-only (no reachable producer)"},
    {403, "214A/B/C (Fire Bolt explosion hit)"},
    {404, "PAT-only (no reachable producer)"},
    {405, "421A/B/C / 214214A/B (ice VFX)"},
    {406, "5S / magic spell (cast visual)"},
    {407, "5S / magic spell / 236S (cast visual)"},
    {408, "236S (cast visual)"},
    {409, "6C (Napalm Beat hit)"},
    {410, "641236A (Thunderstorm hit)"},
    {411, "automatic long-idle magic gain (visual)"},
    {412, "236236A (Fire Ball travel hit)"},
    {413, "236236A/B (explosion controller)"},
    {414, "2141236A (Soul Strike hit)"},
    {415, "2141236B (Soul Strike hit)"},
    {416, "2141236C (Soul Strike hit)"},
    {419, "641236B (Jupiter Thunder initial hit)"},
    {420, "641236B (Jupiter Thunder repeating hit)"},
    {421, "236236A/B (explosion hit)"},
    {422, "2141236A/B/C (Soul Strike trail VFX)"},
    {423, "214214A (Frost Diver hit)"},
    {424, "214214A (travel VFX)"},
    {425, "214214A (travel VFX)"},
    {426, "214214A (travel VFX)"},
    {427, "214214A (travel VFX)"},
    {428, "214214B (Frost Nova hit)"},
    {429, "236236B (Fire Wall pillar hit)"},
    {430, "22B (Safety Wall startup controller)"},
    {431, "22B (persistent Safety Wall)"},
    {432, "214214C (Storm Gust hit)"},
    {433, "214214C (terminal VFX)"},
    {434, "214214C (moving/root hit)"},
    {435, "421A/B/C (Cold Bolt hit variant)"},
    {436, "421A/B/C (Cold Bolt hit variant)"},
    {437, "421A/B/C (Cold Bolt hit variant)"},
    {438, "421A/B/C (Cold Bolt hit variant)"},
    {439, "421A/B/C (Cold Bolt hit variant)"},
    {440, "421A (Cold Bolt controller)"},
    {441, "421B (Cold Bolt controller)"},
    {442, "421C (Cold Bolt controller)"},
    {443, "236A (Lightning Bolt controller)"},
    {444, "236B (Lightning Bolt controller)"},
    {445, "236C (Lightning Bolt controller)"},
    {446, "641236C (Lord of Vermilion hit)"},
    {447, "641236C / 236236C (visual helper)"},
    {448, "641236C (pulse hit)"},
    {449, "641236C (root controller)"},
    {450, "236236C (rising meteor hit)"},
    {451, "236236C (falling meteor hit)"},
    {452, "236236C (ground-contact hit)"},
    {453, "236236C (ground-contact hit)"},
    {454, "641236C (terminal hit)"},
    {455, "641236A (movable/release controller)"},
    {456, "2141236S FM (cutscene/direct-damage helper)"},
};

static const EntityNote kKaori[] = {
    {400, "623B (helper)"},
    {401, "236236C (visual helper)"},
    {402, "236236A/B (automatic-hit helper)"},
    {403, "236236A/B (automatic-hit helper)"},
    {404, "236236A/B (automatic-hit helper)"},
    {405, "236236C (automatic-hit helper)"},
    {406, "236236C (automatic-hit helper)"},
    {407, "236236C (automatic-hit helper)"},
    {410, "623A"},
    {411, "623C (helper)"},
};

static const EntityNote kMai[] = {
    {400, "PAT-only (no reachable producer)"},
    {401, "5S"},
    {402, "5S (forced-retreat phase)"},
    {403, "5S (time-expiry phase)"},
    {404, "5S (recall phase)"},
    {407, "PAT-only (no reachable producer)"},
    {408, "PAT-only (no reachable producer)"},
    {409, "PAT-only (no reachable producer)"},
    {410, "PAT-only (no reachable producer)"},
    {411, "PAT-only (no reachable producer)"},
    {412, "214A/B/C (charged)"},
    {413, "PAT-only (no reachable producer)"},
    {414, "(j.)22S (summoned attack)"},
    {415, "6A / Awakened 6A (visual helper)"},
    {416, "6B / Awakened 6B (visual helper)"},
    {417, "6C / Awakened 6C (visual helper)"},
    {418, "j.623C (visual helper)"},
    {419, "236236C (contact-cinematic helper)"},
    {420, "236236C (contact-cinematic helper)"},
    {421, "PAT-only (no reachable producer)"},
    {422, "PAT-only (no reachable producer)"},
    {423, "PAT-only (no reachable producer)"},
    {424, "214A/B/C (Awakened)"},
    {425, "PAT-only (no reachable producer)"},
    {426, "PAT-only (no reachable producer)"},
    {427, "236236S (Awakening helper)"},
    {428, "236A (unsummoned attack)"},
    {429, "236B (unsummoned attack)"},
    {430, "236C (unsummoned attack)"},
    {431, "(j.)236S (summoned attack)"},
    {432, "623A/B/C (controller)"},
    {433, "PAT-only (no reachable producer)"},
    {434, "PAT-only (no reachable producer)"},
    {435, "PAT-only (no reachable producer)"},
    {436, "(j.)412S (summoned controller)"},
    {437, "PAT-only (no reachable producer)"},
    {438, "214214A (unsummoned controller)"},
    {439, "214214A (summoned controller)"},
    {440, "214214A (hit)"},
    {441, "214214A/B/C (summoned fireball hit)"},
    {442, "214214B (unsummoned controller)"},
    {443, "214214B (summoned controller)"},
    {444, "214214B (hit)"},
    {445, "PAT-only (no reachable producer)"},
    {446, "214214C (unsummoned controller)"},
    {447, "214214C (summoned controller)"},
    {448, "214214C (hit)"},
    {449, "PAT-only (no reachable producer)"},
    {450, "236236S (Awakened contact helper)"},
    {451, "(j.)214S (summoned attack)"},
    {452, "PAT-only (no reachable producer)"},
    {453, "623A/B/C (hit)"},
    {454, "(j.)412S (hit)"},
};

static const EntityNote kMakoto[] = {
    {401, "214214A/B/C / normal attack (visual helper)"},
    {402, "412A/B/C (helper)"},
    {403, "236A"},
    {404, "236B"},
    {405, "236C"},
    {406, "214A / j.214A (controller)"},
    {407, "214B (controller)"},
    {408, "214C (controller)"},
    {409, "214A/B/C / j.214A (shared hit)"},
    {410, "412A"},
    {411, "412B"},
    {412, "412C"},
    {415, "214214A"},
    {416, "214214B"},
    {417, "214214C"},
    {418, "236236A"},
    {419, "236236B"},
    {420, "236236C"},
    {421, "214214A/B/C (helper)"},
    {423, "641236A/B/C (controller)"},
    {424, "641236A/B/C (helper)"},
};

static const EntityNote kMayu[] = {
    {401, "FMA/action 303 (internal helper)"},
    {402, "FMA/action 303 (internal helper)"},
    {403, "FMA/action 303 (landing phase)"},
    {404, "FMA/action 303 (landing phase)"},
    {405, "FMA/action 303 (internal helper)"},
};

static const EntityNote kMinagi[] = {
    {400, "round/reset Michiru root / shared unreadied return"},
    {401, "5S (Michiru readied root)"},
    {402, "4S/6S reposition (same slot, duplicate PAT ID)"},
    {403, "Michiru hit/down recovery (same slot)"},
    {404, "(j.)236A unreadied Michiru hit"},
    {405, "(j.)236B unreadied Michiru hit"},
    {406, "(j.)236C unreadied Michiru hit"},
    {407, "(j.)421A Michiru hit"},
    {408, "(j.)421B Michiru hit"},
    {409, "(j.)421C Michiru hit"},
    {410, "(j.)214A unreadied Michiru hit"},
    {411, "(j.)214B unreadied Michiru hit"},
    {412, "(j.)214C unreadied Michiru hit"},
    {416, "(j.)214214A unreadied Michiru hit"},
    {417, "(j.)214214A readied Michiru hit"},
    {418, "(j.)214214B unreadied Michiru hit"},
    {419, "(j.)214214B readied Michiru hit"},
    {420, "(j.)214214C unreadied Michiru hit"},
    {421, "(j.)214214C readied Michiru hit"},
    {422, "(j.)236A readied Michiru hit"},
    {423, "(j.)236B readied Michiru hit"},
    {424, "(j.)236C readied Michiru multihit"},
    {425, "(j.)236A/B/C unreadied bounce recovery"},
    {426, "(j.)214A readied Michiru hit"},
    {427, "(j.)214B readied Michiru hit"},
    {428, "(j.)214C readied Michiru hit"},
    {429, "41236A Rice Ticket projectile"},
    {430, "41236B Rice Ticket projectile"},
    {431, "41236C Rice Ticket projectile"},
    {432, "41236C non-attacking tracker"},
    {436, "Michiru automatic catch-up controller"},
    {437, "2141236A/B/C bound Michiru controller"},
    {440, "2141236A Soap Bubble projectile"},
    {441, "2141236B Soap Bubble projectile"},
    {442, "2141236C Soap Bubble projectile"},
    {443, "2141236A/B/C emitted Michiru hit"},
    {446, "222S FM vulnerable controller"},
    {447, "222S FM damaging star"},
    {448, "222S FM VFX/helper"},
    {449, "222S FM VFX/helper"},
    {450, "222S FM VFX/helper"},
    {451, "222S FM VFX/helper"},
    {452, "222S FM VFX/helper"},
    {453, "air throw success (direct-1200 controller)"},
    {454, "(j.)236236A Kick Festival Michiru hit"},
    {455, "(j.)236236B Kick Festival Michiru hit"},
    {456, "(j.)236236C Kick Festival Michiru hit"},
    {457, "236236A / j.236236A readied Michiru hit"},
    {458, "236236B / j.236236B readied Michiru hit"},
    {459, "236236C / j.236236C readied Michiru hit"},
    {463, "(j.)236236A Kick Festival VFX"},
    {464, "(j.)236236B Kick Festival VFX"},
    {465, "(j.)236236C Kick Festival VFX"},
    {466, "623A Drilling Bow Thigh Michiru hit"},
    {467, "623B Drilling Bow Thigh Michiru hit"},
    {468, "623C Drilling Bow Thigh Michiru hit"},
    {469, "Michiru owner-state retirement/recovery"},
};

static const EntityNote kMio[] = {
    {404, "412A/B (L) hit"},
    {405, "236A/B (L) hit"},
    {406, "236C (L) hit"},
    {407, "236A/B~236A (S) helper"},
    {408, "236A/B~236A (S) helper"},
    {409, "214A/B/C (L) terminal VFX"},
    {410, "214A/B/C (L) descending hit child"},
    {411, "236236A/B/C (S) randomized hit"},
    {412, "236236A/B/C (S) randomized hit"},
    {413, "236236A/B/C (S) randomized hit"},
    {414, "236236A/B/C (S) emitted VFX"},
    {415, "236236A/B/C (S) emitted VFX"},
    {416, "236236A/B/C (S) emitted VFX"},
    {417, "236236A/B/C (S) emitted VFX"},
    {418, "236236A (L) hit"},
    {419, "236236B/C (L) emitted VFX"},
    {422, "214A/B/C (L) emitted hit"},
    {424, "236236B (L) hit"},
    {425, "236236C (L) hit"},
    {426, "214214A/B/C (L) repeated hit"},
    {427, "FM (L) choreography helper/controller"},
    {428, "FM (L) choreography helper/controller"},
    {429, "FM (L) choreography helper/controller"},
    {430, "FM (L) choreography helper/controller"},
    {431, "FM (L) choreography helper/controller"},
    {432, "FM (L) choreography helper/controller"},
    {433, "FM (L) choreography helper/controller"},
    {434, "FM (L) choreography helper/controller"},
    {435, "FM (L) choreography helper/controller"},
    {436, "FM (L) choreography helper/controller"},
    {437, "FM (L) choreography helper/controller"},
    {438, "FM (L) choreography helper/controller"},
    {439, "FM (L) hit-child controller"},
    {440, "FM (L) choreography helper/controller"},
    {441, "FM (L) choreography helper/controller"},
    {442, "FM (S) 236C ender hit"},
    {443, "FM (L) choreography helper"},
    {444, "412C (L) hit"},
    {445, "623C (S) owner-action VFX"},
    {446, "214214A/B/C (S) helper"},
    {447, "214214A/B/C (S) helper"},
    {450, "air throw success VFX/controller"},
    {451, "air throw success VFX/controller"},
    {452, "air throw success VFX/controller"},
    {453, "214A/B/C (L) controller"},
    {454, "ground-throw success helper"},
    {455, "236A/B~236B (S) emitted VFX"},
    {456, "236A/B~236B (S) hit"},
};

static const EntityNote kMisaki[] = {
    {400, "5C / 6321463214A contact helper"},
    {401, "22A/B counter / 6321463214C / FM helper"},
    {402, "FM counter-follow-up helper"},
    {403, "j.236236B / 6321463214B/C contact hit"},
    {404, "j.236236B relay/controller"},
    {405, "j.236236B terminal hit"},
    {406, "236A hit/root"},
    {407, "236B hit/root"},
    {408, "236C emitted hit"},
    {409, "214C hit"},
    {410, "236A/B later same-slot hit"},
    {411, "ground 236236A hit"},
    {412, "ground 236236A child/helper"},
    {413, "ground 236236B hit"},
    {414, "ground 236236B child/helper"},
    {415, "ground 236236C hit"},
    {416, "ground 236236C child/helper"},
    {417, "236C setup controller"},
    {418, "6321463214A/B/C starter helper"},
    {419, "j.236236A repeating hit/controller"},
    {421, "j.236236A relay/controller"},
    {422, "j.236236A terminal hit"},
    {423, "j.236236C repeating hit/controller"},
    {425, "j.236236C relay/controller"},
    {426, "j.236236C terminal hit"},
};

static const EntityNote kMishio[] = {
    {400, "412A Lightning projectile"},
    {401, "412B Lightning projectile"},
    {402, "412C Lightning projectile"},
    {403, "(j.)214A/B/C trail/helper"},
    {404, "(j.)214A/B/C trail/helper"},
    {405, "(j.)214A/B/C trail/helper"},
    {406, "(j.)214A/B/C trail/helper"},
    {407, "(j.)214A/B/C trail/helper"},
    {408, "22A/B delayed-Lightning controller"},
    {409, "22A/B emitted Lightning hit"},
    {412, "214214A/B/C trail/helper"},
    {413, "214214A/B/C trail/helper"},
    {414, "214214A/B/C trail/helper"},
    {415, "214214A/B/C trail/helper"},
    {416, "214214A/B/C trail/helper"},
    {417, "641236A/B/C Lightning hit"},
    {418, "641236A/B/C Lightning hit"},
    {419, "641236A/B/C Lightning hit"},
    {420, "641236A/B/C Lightning hit"},
    {421, "641236 / j.236236 Lightning controller"},
    {422, "j.236236A/B/C Lightning hit"},
    {423, "j.236236A/B/C Lightning hit"},
    {424, "j.236236A/B/C Lightning hit"},
    {425, "j.236236A/B/C Lightning hit"},
    {426, "j.236236A/B/C Lightning hit"},
    {427, "j.236236A/B/C Lightning hit"},
    {428, "j.236236A/B/C Lightning hit"},
    {429, "641236 Lightning hit / FM hit"},
    {430, "641236A/B/C Lightning hit"},
    {431, "641236A/B/C Lightning hit"},
    {432, "641236A/B/C Lightning hit"},
    {433, "641236A/B/C Lightning hit"},
    {434, "641236A/B/C Lightning hit"},
    {435, "641236 Lightning hit / FM hit"},
    {436, "FM helper"},
    {437, "623A/B/C Fire/Lightning helper"},
    {438, "623A/B/C Fire/Lightning helper"},
    {439, "623A/B/C Fire/Lightning helper"},
    {440, "623A/B/C Fire/Lightning helper"},
    {441, "623A/B/C Lightning emitted hit"},
    {442, "623A/B/C Lightning hit/root"},
    {443, "623A/B/C Lightning hit/root"},
    {444, "623A/B/C Fire/Lightning helper"},
    {445, "623A/B/C Fire/Lightning helper"},
};

static const EntityNote kMisuzu[] = {
    {403, "214A trap lifecycle"},
    {404, "214B trap lifecycle"},
    {405, "214C trap lifecycle"},
    {406, "236A proximity trap"},
    {407, "236B proximity trap"},
    {408, "236C proximity trap"},
    {413, "421A attached source"},
    {414, "421A launch helper"},
    {416, "421B attached source"},
    {417, "421B launch helper"},
    {419, "421C attached source"},
    {420, "421C launch helper"},
    {424, "421A/B/C spent state"},
    {425, "j.214214A hit"},
    {426, "214214B hit"},
    {427, "214214C hit"},
    {435, "214214A/B/C follow-up setup/helper"},
    {436, "236236A controller"},
    {437, "236236A hit"},
    {438, "236236B controller"},
    {439, "236236B hit"},
    {440, "236236C controller"},
    {441, "236236C hit"},
    {448, "214214A/B/C follow-up helper"},
    {449, "FM action 316 helper/controller"},
    {450, "FM action 316 helper/controller"},
    {453, "421A final projectile (then spent state)"},
    {454, "421B final projectile (then spent state)"},
    {455, "421C final projectile (then spent state)"},
    {456, "421A travelling phase"},
    {457, "421B travelling phase"},
    {458, "421C travelling phase"},
    {459, "421A final-attack spawner"},
    {460, "421B final-attack spawner"},
    {461, "421C final-attack spawner"},
};

static const EntityNote kMizukab[] = {
    {406, "214A/B giant-plush hit"},
    {408, "214C repeating plush hit"},
    {409, "214C repeating plush hit"},
    {410, "214C repeating plush hit"},
    {411, "236236A/B emitted herd hit"},
    {412, "236236A/B emitted herd hit"},
    {413, "236236A/B emitted herd hit"},
    {414, "236236A/B emitted herd hit"},
    {415, "236236A/B emitted herd hit"},
    {416, "236236A/B emitted herd hit"},
    {417, "236A/B / j.236A/B orb source"},
    {418, "236A/B / j.236A/B orb source"},
    {419, "(j.)236C slow expanded orb"},
    {420, "(j.)236A/B expanded-orb hit"},
    {421, "FM follow-up 315 hit"},
    {422, "FM follow-up 315 helper/controller"},
    {423, "FM follow-up 315 helper/controller"},
    {424, "FM follow-up 315 helper/controller"},
    {425, "FM follow-up 315 helper/controller"},
    {426, "FM follow-up 315 helper/controller"},
    {428, "FM follow-up 315 helper/controller"},
    {429, "FM follow-up 315 helper/controller"},
    {430, "FM follow-up 315 helper/controller"},
    {431, "FM follow-up 315 helper/controller"},
    {432, "FM follow-up 315 helper/controller"},
    {433, "FM follow-up 315 helper/controller"},
    {434, "FM follow-up 315 helper/controller"},
    {437, "623A/B/C explosion VFX"},
    {438, "FM follow-up 315 helper/controller"},
    {439, "463214A/B/C on-hit result helper"},
    {440, "463214A/B/C barrel contact entity"},
    {444, "41236A/B/C knife projectile (wave unresolved)"},
    {445, "41236A/B/C knife projectile (wave unresolved)"},
    {446, "41236A/B/C knife projectile (wave unresolved)"},
    {447, "41236A/B/C knife projectile (wave unresolved)"},
    {448, "41236A/B/C knife projectile (wave unresolved)"},
    {449, "41236A/B/C knife projectile (wave unresolved)"},
    {450, "41236A/B/C knife projectile (wave unresolved)"},
    {451, "41236A/B/C knife projectile (wave unresolved)"},
    {453, "463214A/B/C on-hit result helper"},
    {454, "463214A/B/C on-hit result helper"},
    {455, "463214A/B/C on-hit result helper"},
    {461, "236236A/B herd controller"},
    {462, "236236A/B herd controller"},
    {464, "j.236236A Solar source phase"},
    {465, "j.236236A terminal blast hit"},
    {466, "j.236236B Solar source phase"},
    {467, "j.236236B terminal blast hit"},
    {468, "j.236236A/B/C trail helper"},
    {469, "j.236236C Solar source phase"},
    {470, "j.236236C terminal blast hit"},
};

static const EntityNote kNagamori[] = {
    {400, "214A / j.236A / j.214A ordinary note, plus 236C child"},
    {401, "(j.)214214C nonstandard note/source"},
    {402, "(j.)214214A Forte note"},
    {403, "(j.)214214B Treble note"},
    {404, "2C note-range marker"},
    {405, "shared ordinary note-explosion hit"},
    {406, "214B / j.236B / j.214B ordinary note"},
    {407, "214C ground ordinary note"},
    {408, "j.236C / j.214C enhanced note"},
    {409, "214214C-created triggerable note/source"},
    {411, "236A bow projectile"},
    {412, "236B bow projectile"},
    {413, "236C bow projectile"},
    {414, "236C bow VFX/helper"},
    {415, "214214C-created triggerable note/source"},
    {417, "FM controller"},
    {418, "FM fanout helper/controller"},
    {419, "FM fanout helper/controller"},
    {420, "FM fanout helper/controller"},
    {421, "FM fanout helper/controller"},
    {422, "FM fanout helper/controller"},
    {423, "FM fanout helper/controller"},
    {424, "FM fanout helper/controller"},
    {425, "FM fanout helper/controller"},
    {426, "FM fanout helper/controller"},
    {427, "FM fanout helper/controller"},
    {428, "FM fanout helper/controller"},
    {429, "FM fanout helper/controller"},
    {430, "FM fanout helper/controller"},
    {431, "FM fanout helper/controller"},
    {432, "FM fanout helper/controller"},
    {433, "FM fanout helper/controller"},
    {434, "FM fanout helper/controller"},
    {435, "FM fanout helper/controller"},
    {436, "FM fanout helper/controller"},
    {437, "FM fanout helper/controller"},
    {438, "FM fanout helper/controller"},
    {439, "FM fanout helper/controller"},
    {440, "FM fanout helper/controller"},
    {441, "FM fanout helper/controller"},
    {442, "FM fanout helper/controller"},
    {443, "FM fanout helper/controller"},
    {444, "FM fanout helper/controller"},
    {445, "FM fanout helper/controller"},
    {446, "FM fanout helper/controller"},
    {447, "FM auxiliary controller"},
    {448, "FM auxiliary terminal helper"},
    {449, "214214C-created triggerable note/source"},
    {450, "641236A initial explosion"},
    {451, "641236B initial explosion"},
    {452, "641236C initial explosion / note-range trigger"},
    {453, "641236A/B/C shared child/helper"},
    {454, "Treble note-explosion hit"},
};

static const EntityNote kNanase[] = {
    {400, "623A/B thrown Shinai"},
    {401, "623A/B/C Shinai companion/VFX"},
    {402, "623A/B Shinai landing hit"},
    {403, "623A/B/C landed/explosion phase"},
    {404, "623A/B/C terminal helper"},
    {405, "214C shockwave bullet"},
    {406, "214C shockwave bullet"},
    {407, "214C shockwave bullet"},
    {408, "214C shockwave bullet"},
    {409, "214C blank/helper/VFX result"},
    {410, "214C blank/helper/VFX result"},
    {411, "214A/B helper / 214C helper result"},
    {412, "FM / alternate-super helper VFX"},
    {413, "623C alternate-table Shinai"},
    {414, "623C Shinai landing hit"},
    {419, "4123641236A root/internal phase"},
    {420, "4123641236B root/internal phase"},
    {421, "4123641236B/C internal phase"},
    {422, "4123641236A internal phase"},
    {423, "4123641236A/B/C companion"},
    {424, "4123641236A/B/C shared terminal phase"},
    {425, "4123641236B internal phase"},
    {426, "4123641236C internal phase"},
    {428, "4123641236C root/internal phase"},
    {429, "214C repeating shockwave controller"},
    {431, "FM / alternate-super helper VFX"},
};

static const EntityNote kNayuki[] = {
    {401, "236A projectile"},
    {402, "236B projectile"},
    {403, "236C projectile"},
    {405, "214214A/B/C terminal phase"},
    {406, "236236A Black Demon controller"},
    {407, "236236B Black Demon controller"},
    {408, "236236C Black Demon controller"},
    {409, "236236A Black Demon hit"},
    {410, "236236B Black Demon hit"},
    {411, "236236C Black Demon hit / FM wall beam"},
    {412, "236236A/B/C shared retirement VFX"},
    {413, "214214A/B/C Beam controller"},
    {414, "214214A/B/C Beam controller"},
    {415, "214214A/B/C emitted hit"},
    {416, "214214A/B/C emitted hit"},
    {417, "FM wall-contact helper"},
    {427, "214*~5B backward projectile"},
    {428, "214*~5B neutral projectile"},
    {429, "214*~5B forward projectile"},
};

static const EntityNote kNayukib[] = {
    {400, "236236A Freezer hit"},
    {401, "236236B Freezer root/hit"},
    {402, "236236C Freezer root/hit"},
    {403, "236236B/C emitted Freezer hit"},
    {404, "641236A/B/C Snowbunny lane-1 hit"},
    {405, "641236A/B/C Snowbunny lane-2 hit"},
    {406, "641236A/B/C Snowbunny lane-1 relay"},
    {407, "641236A/B/C Snowbunny lane-2 relay"},
    {408, "641236A/B/C Snowbunny lane-1 controller"},
    {409, "641236A/B/C Snowbunny lane-2 controller"},
    {411, "641236A/B/C Snowbunny trail"},
    {412, "FM direct root (attack-capable)"},
    {413, "FM direct root (attack-capable)"},
    {414, "FM direct root (attack-capable)"},
    {415, "FM direct root (attack-capable)"},
    {417, "FM direct root (attack-capable)"},
    {418, "FM direct root (attack-capable)"},
    {419, "FM controller"},
    {420, "FM controller (emits hit)"},
    {421, "FM controller (emits hit)"},
    {422, "FM emitted hit"},
    {423, "FM controller (emits hit)"},
    {424, "FM emitted hit"},
    {425, "FM direct root (attack-capable)"},
    {427, "FM helper/fanout"},
    {428, "FM helper/fanout"},
    {429, "FM helper/fanout"},
    {430, "FM helper/fanout"},
    {431, "FM emitted hit"},
    {432, "FM helper/fanout"},
    {433, "FM helper/fanout"},
    {434, "FM helper/fanout"},
    {435, "FM helper/fanout"},
    {436, "FM helper/fanout"},
    {437, "FM helper/fanout"},
};

static const EntityNote kSayuri[] = {
    {401, "236A/B/C ground fireball"},
    {402, "j.236A projectile"},
    {403, "j.412A/B/C / 623A helper"},
    {404, "j.412A/B/C / 623A helper"},
    {405, "2141236A/B/C helper/VFX"},
    {406, "236236A/B/C Magical Agents hit"},
    {407, "236236A/B/C non-attacking child"},
    {408, "236236A/B/C Magical Agents hit"},
    {409, "236236A/B/C Magical Agents hit"},
    {410, "236236A/B/C Magical Agents hit"},
    {411, "236236A/B/C Magical Agents hit"},
    {412, "236236A/B/C Magical Agents hit"},
    {413, "236236A/B/C Magical Agents hit"},
    {414, "FM / auto-follow-up controller/VFX"},
    {415, "FM / auto-follow-up controller/VFX"},
    {416, "6321463214A/B/C Magical Thunder hit"},
    {417, "6321463214A/B/C helper"},
    {419, "j.236B projectile"},
    {420, "j.236C projectile"},
    {422, "air throw success helper/VFX"},
};

static const EntityNote kShiori[] = {
    {401, "236A/B projectile"},
    {402, "j.412A/B projectile"},
    {404, "j.412C projectile"},
    {407, "214A / j.214A controller"},
    {408, "j.236A/B projectile"},
    {409, "214B / j.214B emitted hit"},
    {410, "214B initial hit/controller"},
    {412, "214A / j.214A follow-up helper"},
    {413, "214A / j.214A follow-up helper"},
    {414, "214A / j.214A follow-up helper"},
    {415, "214A / j.214A follow-up helper"},
    {416, "214A / j.214A follow-up helper"},
    {418, "412A/B/C shared hit"},
    {420, "214214A hit"},
    {421, "214214B hit"},
    {422, "214214C hit"},
    {423, "5S shield initial entity"},
    {424, "5S shield lifecycle/response"},
    {425, "5S shield lifecycle/response"},
    {426, "5S shield attack-capable response"},
    {427, "236C projectile"},
    {428, "j.236C projectile"},
    {429, "641236A controller/VFX"},
    {430, "641236B controller/VFX"},
    {431, "641236C controller/VFX"},
    {432, "214214A/B/C retirement phase"},
    {433, "214A / j.214A first hit"},
    {434, "214B relay/controller"},
    {435, "(j.)2141236A/B/C shared hit"},
    {436, "(j.)2141236A/B/C companion"},
    {437, "214214A controller"},
    {438, "214214B controller"},
    {439, "214214C controller"},
    {440, "FM controller"},
    {441, "FM hit"},
    {442, "FM terminal helper"},
    {443, "FM helper"},
    {444, "FM hit"},
    {445, "FM finishing-action controller"},
    {446, "FM finishing-action VFX/controller"},
    {447, "FM finishing-action VFX/controller"},
    {448, "FM finishing-action VFX/controller"},
    {449, "FM finishing-action VFX/controller"},
    {450, "j.214B controller"},
    {451, "j.214B relay/controller"},
    {452, "214C / j.214C controller"},
    {453, "214C / j.214C hit"},
    {454, "214C / j.214C relay"},
};

static const CharTable kCharTables[] = {
    {"akane", kAkane, static_cast<int>(sizeof(kAkane) / sizeof(kAkane[0]))},
    {"akiko", kAkiko, static_cast<int>(sizeof(kAkiko) / sizeof(kAkiko[0]))},
    {"ayu", kAyu, static_cast<int>(sizeof(kAyu) / sizeof(kAyu[0]))},
    {"exnanase", kExnanase, static_cast<int>(sizeof(kExnanase) / sizeof(kExnanase[0]))},
    {"ikumi", kIkumi, static_cast<int>(sizeof(kIkumi) / sizeof(kIkumi[0]))},
    {"kanna", kKanna, static_cast<int>(sizeof(kKanna) / sizeof(kKanna[0]))},
    {"kano", kKano, static_cast<int>(sizeof(kKano) / sizeof(kKano[0]))},
    {"kaori", kKaori, static_cast<int>(sizeof(kKaori) / sizeof(kKaori[0]))},
    {"mai", kMai, static_cast<int>(sizeof(kMai) / sizeof(kMai[0]))},
    {"makoto", kMakoto, static_cast<int>(sizeof(kMakoto) / sizeof(kMakoto[0]))},
    {"mayu", kMayu, static_cast<int>(sizeof(kMayu) / sizeof(kMayu[0]))},
    {"minagi", kMinagi, static_cast<int>(sizeof(kMinagi) / sizeof(kMinagi[0]))},
    {"mio", kMio, static_cast<int>(sizeof(kMio) / sizeof(kMio[0]))},
    {"misaki", kMisaki, static_cast<int>(sizeof(kMisaki) / sizeof(kMisaki[0]))},
    {"mishio", kMishio, static_cast<int>(sizeof(kMishio) / sizeof(kMishio[0]))},
    {"misuzu", kMisuzu, static_cast<int>(sizeof(kMisuzu) / sizeof(kMisuzu[0]))},
    {"mizukab", kMizukab, static_cast<int>(sizeof(kMizukab) / sizeof(kMizukab[0]))},
    {"nagamori", kNagamori, static_cast<int>(sizeof(kNagamori) / sizeof(kNagamori[0]))},
    {"nanase", kNanase, static_cast<int>(sizeof(kNanase) / sizeof(kNanase[0]))},
    {"nayuki", kNayuki, static_cast<int>(sizeof(kNayuki) / sizeof(kNayuki[0]))},
    {"nayukib", kNayukib, static_cast<int>(sizeof(kNayukib) / sizeof(kNayukib[0]))},
    {"sayuri", kSayuri, static_cast<int>(sizeof(kSayuri) / sizeof(kSayuri[0]))},
    {"shiori", kShiori, static_cast<int>(sizeof(kShiori) / sizeof(kShiori[0]))},
};
static const int kCharTableCount =
    static_cast<int>(sizeof(kCharTables) / sizeof(kCharTables[0]));

// Old mission JSON contains these generated strings: keep them for migration recognition and
// as an out-of-manifest fallback. The generated catalog below is the authority.
static const SemanticNote kLegacySemanticNotes[] = {
    {"akane", 409, "akane.shiiko", "SHIIKO ATTACK", PresentationRole::Summon},
    {"akane", 410, "akane.shiiko", "SHIIKO ATTACK", PresentationRole::Summon},
    {"akane", 411, "akane.shiiko", "SHIIKO ATTACK", PresentationRole::Summon},
    {"akane", 413, "akane.shiiko", "SHIIKO ATTACK", PresentationRole::Summon},
    {"akane", 414, "akane.shiiko", "SHIIKO ATTACK", PresentationRole::Summon},
    {"akane", 415, "akane.shiiko", "SHIIKO ATTACK", PresentationRole::Summon},
    {"akane", 416, "akane.shiiko", "SHIIKO ATTACK", PresentationRole::Summon},

    {"akiko", 415, "akiko.cat", "CAT ATTACK", PresentationRole::Summon},
    {"akiko", 416, "akiko.cat", "CAT ATTACK", PresentationRole::Summon},
    {"akiko", 417, "akiko.cat", "CAT ATTACK", PresentationRole::Summon},
    {"akiko", 418, "akiko.cat", "CAT ATTACK", PresentationRole::Summon},
    {"akiko", 419, "akiko.cat", "CAT ATTACK", PresentationRole::Summon},
    {"akiko", 420, "akiko.cat", "CAT ATTACK", PresentationRole::Summon},
    {"akiko", 421, "akiko.cat", "CAT ATTACK", PresentationRole::Summon},

    {"ikumi", 402, "ikumi.skyscraper", "214 SKYSCRAPER", PresentationRole::Trap},
    {"ikumi", 403, "ikumi.skyscraper", "214 SKYSCRAPER", PresentationRole::Trap},
    {"ikumi", 406, "ikumi.skyscraper", "214 SKYSCRAPER", PresentationRole::Trap},
    {"ikumi", 410, "ikumi.skyscraper", "214 SKYSCRAPER", PresentationRole::Trap},
    {"ikumi", 411, "ikumi.skyscraper", "214 SKYSCRAPER", PresentationRole::Trap},
    {"ikumi", 412, "ikumi.skyscraper", "214 SKYSCRAPER", PresentationRole::Trap},
    {"ikumi", 413, "ikumi.skyscraper", "214 SKYSCRAPER", PresentationRole::Trap},
    {"ikumi", 478, "ikumi.skyscraper", "214 SKYSCRAPER", PresentationRole::Trap},
    {"ikumi", 420, "ikumi.clone", "MIRROR CLONE", PresentationRole::Setplay},
    {"ikumi", 421, "ikumi.clone", "MIRROR CLONE", PresentationRole::Setplay},
    {"ikumi", 422, "ikumi.clone", "MIRROR CLONE", PresentationRole::Setplay},
    {"ikumi", 428, "ikumi.clone", "MIRROR CLONE", PresentationRole::Setplay},
    {"ikumi", 429, "ikumi.clone", "MIRROR CLONE", PresentationRole::Setplay},
    {"ikumi", 430, "ikumi.clone", "MIRROR CLONE", PresentationRole::Setplay},
    {"ikumi", 423, "ikumi.blood_bomb", "BLOOD BOMB", PresentationRole::Trap},
    {"ikumi", 424, "ikumi.blood_bomb", "BLOOD BOMB", PresentationRole::Trap},
    {"ikumi", 425, "ikumi.blood_bomb", "BLOOD BOMB", PresentationRole::Trap},
    {"ikumi", 426, "ikumi.blood_bomb", "BLOOD BOMB", PresentationRole::Trap},
    {"ikumi", 427, "ikumi.blood_bomb", "BLOOD BOMB", PresentationRole::Trap},

    {"kanna", 400, "kanna.stone", "214 STONE ERUPTION", PresentationRole::Trap},
    {"kanna", 401, "kanna.stone", "214 STONE ERUPTION", PresentationRole::Trap},
    {"kanna", 402, "kanna.stone", "214 STONE ERUPTION", PresentationRole::Trap},
    {"kanna", 403, "kanna.stone", "214 STONE ERUPTION", PresentationRole::Trap},

    {"kano", 401, "kano.fire_bolt", "FIRE BOLT", PresentationRole::InlineProjectile},
    {"kano", 403, "kano.fire_bolt", "FIRE BOLT", PresentationRole::InlineProjectile},
    {"kano", 419, "kano.jupiter", "JUPITER THUNDER", PresentationRole::InlineProjectile},
    {"kano", 420, "kano.jupiter", "JUPITER THUNDER", PresentationRole::InlineProjectile},
    {"kano", 429, "kano.fire_wall", "FIRE WALL", PresentationRole::Trap},
    {"kano", 430, "kano.safety_wall", "SAFETY WALL", PresentationRole::Trap},
    {"kano", 431, "kano.safety_wall", "SAFETY WALL", PresentationRole::Trap},
    {"kano", 432, "kano.storm_gust", "STORM GUST", PresentationRole::Setplay},
    {"kano", 434, "kano.storm_gust", "STORM GUST", PresentationRole::Setplay},
    {"kano", 446, "kano.lord_vermilion", "LORD OF VERMILION", PresentationRole::InlineProjectile},
    {"kano", 448, "kano.lord_vermilion", "LORD OF VERMILION", PresentationRole::InlineProjectile},
    {"kano", 450, "kano.meteor", "METEOR STORM", PresentationRole::Setplay},
    {"kano", 451, "kano.meteor", "METEOR STORM", PresentationRole::Setplay},
    {"kano", 452, "kano.meteor", "METEOR STORM", PresentationRole::Setplay},
    {"kano", 453, "kano.meteor", "METEOR STORM", PresentationRole::Setplay},
    {"kano", 454, "kano.lord_vermilion", "LORD OF VERMILION", PresentationRole::InlineProjectile},

    {"mai", 401, "mai.mini_mai", "MINI-MAI ATTACK", PresentationRole::Summon},
    {"mai", 402, "mai.mini_mai", "MINI-MAI ATTACK", PresentationRole::Summon},
    {"mai", 403, "mai.mini_mai", "MINI-MAI ATTACK", PresentationRole::Summon},
    {"mai", 404, "mai.mini_mai", "MINI-MAI ATTACK", PresentationRole::Summon},
    {"mai", 414, "mai.mini_mai", "MINI-MAI ATTACK", PresentationRole::Summon},
    {"mai", 424, "mai.mini_mai", "MINI-MAI ATTACK", PresentationRole::Summon},
    {"mai", 428, "mai.mini_mai", "MINI-MAI ATTACK", PresentationRole::Summon},
    {"mai", 429, "mai.mini_mai", "MINI-MAI ATTACK", PresentationRole::Summon},
    {"mai", 430, "mai.mini_mai", "MINI-MAI ATTACK", PresentationRole::Summon},
    {"mai", 431, "mai.mini_mai", "MINI-MAI ATTACK", PresentationRole::Summon},
    {"mai", 436, "mai.mini_mai", "MINI-MAI ATTACK", PresentationRole::Summon},
    {"mai", 438, "mai.mini_mai", "MINI-MAI ATTACK", PresentationRole::Summon},
    {"mai", 439, "mai.mini_mai", "MINI-MAI ATTACK", PresentationRole::Summon},
    {"mai", 440, "mai.mini_mai", "MINI-MAI ATTACK", PresentationRole::Summon},
    {"mai", 441, "mai.mini_mai", "MINI-MAI ATTACK", PresentationRole::Summon},
    {"mai", 442, "mai.mini_mai", "MINI-MAI ATTACK", PresentationRole::Summon},
    {"mai", 443, "mai.mini_mai", "MINI-MAI ATTACK", PresentationRole::Summon},
    {"mai", 444, "mai.mini_mai", "MINI-MAI ATTACK", PresentationRole::Summon},
    {"mai", 446, "mai.mini_mai", "MINI-MAI ATTACK", PresentationRole::Summon},
    {"mai", 447, "mai.mini_mai", "MINI-MAI ATTACK", PresentationRole::Summon},
    {"mai", 448, "mai.mini_mai", "MINI-MAI ATTACK", PresentationRole::Summon},
    {"mai", 451, "mai.mini_mai", "MINI-MAI ATTACK", PresentationRole::Summon},
    {"mai", 453, "mai.mini_mai", "MINI-MAI ATTACK", PresentationRole::Summon},
    {"mai", 454, "mai.mini_mai", "MINI-MAI ATTACK", PresentationRole::Summon},

    {"minagi", 400, "minagi.michiru", "MICHIRU ATTACK", PresentationRole::Summon},
    {"minagi", 401, "minagi.michiru", "MICHIRU ATTACK", PresentationRole::Summon},
    {"minagi", 402, "minagi.michiru", "MICHIRU ATTACK", PresentationRole::Summon},
    {"minagi", 403, "minagi.michiru", "MICHIRU ATTACK", PresentationRole::Summon},
    {"minagi", 404, "minagi.michiru", "MICHIRU ATTACK", PresentationRole::Summon},
    {"minagi", 405, "minagi.michiru", "MICHIRU ATTACK", PresentationRole::Summon},
    {"minagi", 406, "minagi.michiru", "MICHIRU ATTACK", PresentationRole::Summon},
    {"minagi", 407, "minagi.michiru", "MICHIRU ATTACK", PresentationRole::Summon},
    {"minagi", 408, "minagi.michiru", "MICHIRU ATTACK", PresentationRole::Summon},
    {"minagi", 409, "minagi.michiru", "MICHIRU ATTACK", PresentationRole::Summon},
    {"minagi", 410, "minagi.michiru", "MICHIRU ATTACK", PresentationRole::Summon},
    {"minagi", 411, "minagi.michiru", "MICHIRU ATTACK", PresentationRole::Summon},
    {"minagi", 412, "minagi.michiru", "MICHIRU ATTACK", PresentationRole::Summon},
    {"minagi", 416, "minagi.michiru", "MICHIRU ATTACK", PresentationRole::Summon},
    {"minagi", 417, "minagi.michiru", "MICHIRU ATTACK", PresentationRole::Summon},
    {"minagi", 418, "minagi.michiru", "MICHIRU ATTACK", PresentationRole::Summon},
    {"minagi", 419, "minagi.michiru", "MICHIRU ATTACK", PresentationRole::Summon},
    {"minagi", 420, "minagi.michiru", "MICHIRU ATTACK", PresentationRole::Summon},
    {"minagi", 421, "minagi.michiru", "MICHIRU ATTACK", PresentationRole::Summon},
    {"minagi", 422, "minagi.michiru", "MICHIRU ATTACK", PresentationRole::Summon},
    {"minagi", 423, "minagi.michiru", "MICHIRU ATTACK", PresentationRole::Summon},
    {"minagi", 424, "minagi.michiru", "MICHIRU ATTACK", PresentationRole::Summon},
    {"minagi", 425, "minagi.michiru", "MICHIRU ATTACK", PresentationRole::Summon},
    {"minagi", 426, "minagi.michiru", "MICHIRU ATTACK", PresentationRole::Summon},
    {"minagi", 427, "minagi.michiru", "MICHIRU ATTACK", PresentationRole::Summon},
    {"minagi", 428, "minagi.michiru", "MICHIRU ATTACK", PresentationRole::Summon},
    {"minagi", 436, "minagi.michiru", "MICHIRU ATTACK", PresentationRole::Summon},
    {"minagi", 437, "minagi.michiru", "MICHIRU ATTACK", PresentationRole::Summon},
    {"minagi", 443, "minagi.michiru", "MICHIRU ATTACK", PresentationRole::Summon},
    {"minagi", 454, "minagi.michiru", "MICHIRU ATTACK", PresentationRole::Summon},
    {"minagi", 455, "minagi.michiru", "MICHIRU ATTACK", PresentationRole::Summon},
    {"minagi", 456, "minagi.michiru", "MICHIRU ATTACK", PresentationRole::Summon},
    {"minagi", 466, "minagi.michiru", "MICHIRU ATTACK", PresentationRole::Summon},
    {"minagi", 467, "minagi.michiru", "MICHIRU ATTACK", PresentationRole::Summon},
    {"minagi", 468, "minagi.michiru", "MICHIRU ATTACK", PresentationRole::Summon},
    {"minagi", 469, "minagi.michiru", "MICHIRU ATTACK", PresentationRole::Summon},
    {"minagi", 440, "minagi.soap_bubble", "SOAP BUBBLE", PresentationRole::Setplay},
    {"minagi", 441, "minagi.soap_bubble", "SOAP BUBBLE", PresentationRole::Setplay},
    {"minagi", 442, "minagi.soap_bubble", "SOAP BUBBLE", PresentationRole::Setplay},

    {"misaki", 408, "misaki.236c", "236C SETPLAY", PresentationRole::Setplay},
    {"misaki", 417, "misaki.236c", "236C SETPLAY", PresentationRole::Setplay},

    {"mishio", 408, "mishio.delayed_lightning", "DELAYED LIGHTNING", PresentationRole::Trap},
    {"mishio", 409, "mishio.delayed_lightning", "DELAYED LIGHTNING", PresentationRole::Trap},

    {"misuzu", 403, "misuzu.214_trap", "214 TRAP", PresentationRole::Trap},
    {"misuzu", 404, "misuzu.214_trap", "214 TRAP", PresentationRole::Trap},
    {"misuzu", 405, "misuzu.214_trap", "214 TRAP", PresentationRole::Trap},
    {"misuzu", 406, "misuzu.236_trap", "236 TRAP", PresentationRole::Trap},
    {"misuzu", 407, "misuzu.236_trap", "236 TRAP", PresentationRole::Trap},
    {"misuzu", 408, "misuzu.236_trap", "236 TRAP", PresentationRole::Trap},
    {"misuzu", 413, "misuzu.421a", "421A PROJECTILE", PresentationRole::Setplay},
    {"misuzu", 414, "misuzu.421a", "421A PROJECTILE", PresentationRole::Setplay},
    {"misuzu", 453, "misuzu.421a", "421A PROJECTILE", PresentationRole::Setplay},
    {"misuzu", 456, "misuzu.421a", "421A PROJECTILE", PresentationRole::Setplay},
    {"misuzu", 459, "misuzu.421a", "421A PROJECTILE", PresentationRole::Setplay},
    {"misuzu", 416, "misuzu.421b", "421B PROJECTILE", PresentationRole::Setplay},
    {"misuzu", 417, "misuzu.421b", "421B PROJECTILE", PresentationRole::Setplay},
    {"misuzu", 454, "misuzu.421b", "421B PROJECTILE", PresentationRole::Setplay},
    {"misuzu", 457, "misuzu.421b", "421B PROJECTILE", PresentationRole::Setplay},
    {"misuzu", 460, "misuzu.421b", "421B PROJECTILE", PresentationRole::Setplay},
    {"misuzu", 419, "misuzu.421c", "421C PROJECTILE", PresentationRole::Setplay},
    {"misuzu", 420, "misuzu.421c", "421C PROJECTILE", PresentationRole::Setplay},
    {"misuzu", 455, "misuzu.421c", "421C PROJECTILE", PresentationRole::Setplay},
    {"misuzu", 458, "misuzu.421c", "421C PROJECTILE", PresentationRole::Setplay},
    {"misuzu", 461, "misuzu.421c", "421C PROJECTILE", PresentationRole::Setplay},

    {"mizukab", 406, "mizukab.plush", "GIANT PLUSH", PresentationRole::Setplay},
    {"mizukab", 408, "mizukab.plush", "GIANT PLUSH", PresentationRole::Setplay},
    {"mizukab", 409, "mizukab.plush", "GIANT PLUSH", PresentationRole::Setplay},
    {"mizukab", 410, "mizukab.plush", "GIANT PLUSH", PresentationRole::Setplay},
    {"mizukab", 417, "mizukab.orb", "ORB ACTIVATION", PresentationRole::Setplay},
    {"mizukab", 418, "mizukab.orb", "ORB ACTIVATION", PresentationRole::Setplay},
    {"mizukab", 419, "mizukab.orb", "ORB ACTIVATION", PresentationRole::Setplay},
    {"mizukab", 420, "mizukab.orb", "ORB ACTIVATION", PresentationRole::Setplay},
    {"mizukab", 444, "mizukab.knife_wave", "KNIFE WAVE", PresentationRole::InlineProjectile},
    {"mizukab", 445, "mizukab.knife_wave", "KNIFE WAVE", PresentationRole::InlineProjectile},
    {"mizukab", 446, "mizukab.knife_wave", "KNIFE WAVE", PresentationRole::InlineProjectile},
    {"mizukab", 447, "mizukab.knife_wave", "KNIFE WAVE", PresentationRole::InlineProjectile},
    {"mizukab", 448, "mizukab.knife_wave", "KNIFE WAVE", PresentationRole::InlineProjectile},
    {"mizukab", 449, "mizukab.knife_wave", "KNIFE WAVE", PresentationRole::InlineProjectile},
    {"mizukab", 450, "mizukab.knife_wave", "KNIFE WAVE", PresentationRole::InlineProjectile},
    {"mizukab", 451, "mizukab.knife_wave", "KNIFE WAVE", PresentationRole::InlineProjectile},

    {"nagamori", 400, "nagamori.note", "NOTE EXPLOSION", PresentationRole::Setplay},
    {"nagamori", 402, "nagamori.note", "NOTE EXPLOSION", PresentationRole::Setplay},
    {"nagamori", 405, "nagamori.note", "NOTE EXPLOSION", PresentationRole::Setplay},
    {"nagamori", 406, "nagamori.note", "NOTE EXPLOSION", PresentationRole::Setplay},
    {"nagamori", 407, "nagamori.note", "NOTE EXPLOSION", PresentationRole::Setplay},
    {"nagamori", 408, "nagamori.note", "NOTE EXPLOSION", PresentationRole::Setplay},
    {"nagamori", 409, "nagamori.note", "NOTE EXPLOSION", PresentationRole::Setplay},
    {"nagamori", 415, "nagamori.note", "NOTE EXPLOSION", PresentationRole::Setplay},
    {"nagamori", 449, "nagamori.note", "NOTE EXPLOSION", PresentationRole::Setplay},
    {"nagamori", 403, "nagamori.treble", "TREBLE EXPLOSION", PresentationRole::Setplay},
    {"nagamori", 454, "nagamori.treble", "TREBLE EXPLOSION", PresentationRole::Setplay},
    {"nagamori", 411, "nagamori.bow", "BOW PROJECTILE", PresentationRole::InlineProjectile},
    {"nagamori", 412, "nagamori.bow", "BOW PROJECTILE", PresentationRole::InlineProjectile},
    {"nagamori", 413, "nagamori.bow", "BOW PROJECTILE", PresentationRole::InlineProjectile},

    {"nanase", 400, "nanase.shinai", "THROWN SHINAI", PresentationRole::Setplay},
    {"nanase", 401, "nanase.shinai", "THROWN SHINAI", PresentationRole::Setplay},
    {"nanase", 402, "nanase.shinai", "THROWN SHINAI", PresentationRole::Setplay},
    {"nanase", 403, "nanase.shinai", "THROWN SHINAI", PresentationRole::Setplay},
    {"nanase", 404, "nanase.shinai", "THROWN SHINAI", PresentationRole::Setplay},
    {"nanase", 413, "nanase.shinai", "THROWN SHINAI", PresentationRole::Setplay},
    {"nanase", 414, "nanase.shinai", "THROWN SHINAI", PresentationRole::Setplay},
    {"nanase", 405, "nanase.shockwave", "214C SHOCKWAVE", PresentationRole::Setplay},
    {"nanase", 406, "nanase.shockwave", "214C SHOCKWAVE", PresentationRole::Setplay},
    {"nanase", 407, "nanase.shockwave", "214C SHOCKWAVE", PresentationRole::Setplay},
    {"nanase", 408, "nanase.shockwave", "214C SHOCKWAVE", PresentationRole::Setplay},
    {"nanase", 409, "nanase.shockwave", "214C SHOCKWAVE", PresentationRole::Setplay},
    {"nanase", 410, "nanase.shockwave", "214C SHOCKWAVE", PresentationRole::Setplay},
    {"nanase", 411, "nanase.shockwave", "214C SHOCKWAVE", PresentationRole::Setplay},
    {"nanase", 429, "nanase.shockwave", "214C SHOCKWAVE", PresentationRole::Setplay},

    {"nayukib", 400, "nayukib.freezer", "FREEZER", PresentationRole::InlineProjectile},
    {"nayukib", 401, "nayukib.freezer", "FREEZER", PresentationRole::InlineProjectile},
    {"nayukib", 402, "nayukib.freezer", "FREEZER", PresentationRole::InlineProjectile},
    {"nayukib", 403, "nayukib.freezer", "FREEZER", PresentationRole::InlineProjectile},
    {"nayukib", 404, "nayukib.snowbunny", "641236 SNOWBUNNIES", PresentationRole::Setplay},
    {"nayukib", 405, "nayukib.snowbunny", "641236 SNOWBUNNIES", PresentationRole::Setplay},
    {"nayukib", 406, "nayukib.snowbunny", "641236 SNOWBUNNIES", PresentationRole::Setplay},
    {"nayukib", 407, "nayukib.snowbunny", "641236 SNOWBUNNIES", PresentationRole::Setplay},
    {"nayukib", 408, "nayukib.snowbunny", "641236 SNOWBUNNIES", PresentationRole::Setplay},
    {"nayukib", 409, "nayukib.snowbunny", "641236 SNOWBUNNIES", PresentationRole::Setplay},
    {"nayukib", 411, "nayukib.snowbunny", "641236 SNOWBUNNIES", PresentationRole::Setplay},

    {"shiori", 423, "shiori.shield", "SHIELD RESPONSE", PresentationRole::Trap},
    {"shiori", 424, "shiori.shield", "SHIELD RESPONSE", PresentationRole::Trap},
    {"shiori", 425, "shiori.shield", "SHIELD RESPONSE", PresentationRole::Trap},
    {"shiori", 426, "shiori.shield", "SHIELD RESPONSE", PresentationRole::Trap},
};
static const int kLegacySemanticNoteCount =
    static_cast<int>(sizeof(kLegacySemanticNotes) /
                     sizeof(kLegacySemanticNotes[0]));

#include "game/mission/entity_semantic_catalog.generated.h"

// Exact producer lookup never falls back to a wildcard row: this distinguishes
// a catalog-proven setter from a merely plausible move active at spawn time.
inline const SemanticNote* LookupExactProducerSemantic(const char* charName,
                                                       int pattern,
                                                       int producerMove) {
    if (!charName || pattern < 0 || pattern > 0xFFFF || producerMove <= 0) {
        return nullptr;
    }
    for (int i = 0; i < kGeneratedSemanticNoteCount; ++i) {
        const SemanticNote& note = kGeneratedSemanticNotes[i];
        if (note.pattern == static_cast<std::uint16_t>(pattern) &&
            note.producerMove == producerMove &&
            std::strcmp(note.character, charName) == 0) {
            return &note;
        }
    }
    return nullptr;
}

inline const SemanticNote* LookupWildcardSemantic(const char* charName,
                                                  int pattern) {
    if (!charName || pattern < 0 || pattern > 0xFFFF) return nullptr;
    for (int i = 0; i < kGeneratedSemanticNoteCount; ++i) {
        const SemanticNote& note = kGeneratedSemanticNotes[i];
        if (note.pattern == static_cast<std::uint16_t>(pattern) &&
            note.producerMove < 0 &&
            std::strcmp(note.character, charName) == 0) {
            return &note;
        }
    }
    return nullptr;
}

inline const SemanticNote* LookupSemantic(const char* charName, int pattern,
                                          int producerMove = -1) {
    if (!charName || pattern < 0 || pattern > 0xFFFF) return nullptr;

    if (const SemanticNote* exact =
            LookupExactProducerSemantic(charName, pattern, producerMove)) {
        return exact;
    }
    if (const SemanticNote* wildcard =
            LookupWildcardSemantic(charName, pattern)) {
        return wildcard;
    }
    for (int i = 0; i < kLegacySemanticNoteCount; ++i) {
        const SemanticNote& note = kLegacySemanticNotes[i];
        if (note.pattern == static_cast<std::uint16_t>(pattern) &&
            std::strcmp(note.character, charName) == 0) {
            return &note;
        }
    }
    return nullptr;
}

inline bool LifecyclePromisesContact(LifecycleDisposition disposition) {
    return disposition == LifecycleDisposition::ContactEffect ||
           disposition == LifecycleDisposition::Unresolved;
}

inline bool CanOwnRecordedContact(LifecycleDisposition disposition) {
    return LifecyclePromisesContact(disposition);
}

inline bool HasResolvedContactPresentation(
    LifecycleDisposition disposition) {
    return disposition == LifecycleDisposition::ContactEffect;
}

inline bool IsPostContactRecovery(LifecycleDisposition disposition) {
    return disposition == LifecycleDisposition::PostContactRecovery;
}

inline const char* DispositionLabel(LifecycleDisposition disposition) {
    switch (disposition) {
        case LifecycleDisposition::ContactEffect:       return "contact";
        case LifecycleDisposition::Controller:          return "controller";
        case LifecycleDisposition::PostContactRecovery: return "post-contact";
        case LifecycleDisposition::VisualEffect:        return "vfx";
        case LifecycleDisposition::DormantOrOrphan:     return "orphan";
        default:                                        return "unresolved";
    }
}

inline bool IsAlwaysStandalone(PresentationRole role) {
    return role == PresentationRole::Setplay ||
           role == PresentationRole::Trap ||
           role == PresentationRole::Summon;
}

inline bool HasProducerQualifiedPersistentContactVariant(
    const char* charName, int pattern) {
    if (!charName || pattern < 0 || pattern > 0xFFFF) return false;
    for (int i = 0; i < kGeneratedSemanticNoteCount; ++i) {
        const SemanticNote& note = kGeneratedSemanticNotes[i];
        if (note.pattern == static_cast<std::uint16_t>(pattern) &&
            note.producerMove >= 0 && IsAlwaysStandalone(note.role) &&
            HasResolvedContactPresentation(note.disposition) &&
            std::strcmp(note.character, charName) == 0) {
            return true;
        }
    }
    return false;
}

inline const char* RoleLabel(PresentationRole role) {
    switch (role) {
        case PresentationRole::Trap:   return "TRAP";
        case PresentationRole::Summon: return "SUMMON";
        case PresentationRole::Setplay:return "SETPLAY";
        default:                       return "PROJECTILE";
    }
}

inline const char* Lookup(const char* charName, int pattern) {
    if (!charName || pattern < 0 || pattern > 0xFFFF) return nullptr;
    for (int tableIndex = 0; tableIndex < kCharTableCount; ++tableIndex) {
        const CharTable& table = kCharTables[tableIndex];
        if (std::strcmp(table.name, charName) != 0) continue;
        for (int entryIndex = 0; entryIndex < table.count; ++entryIndex) {
            if (table.entries[entryIndex].pattern ==
                static_cast<std::uint16_t>(pattern)) {
                return table.entries[entryIndex].note;
            }
        }
        return nullptr;
    }
    return nullptr;
}

inline bool IsAsciiAlphaNumeric(char value) {
    return (value >= '0' && value <= '9') ||
           (value >= 'A' && value <= 'Z') ||
           (value >= 'a' && value <= 'z');
}

inline bool ContainsOutcomeCaseInsensitive(const std::string& text,
                                           const char* needle) {
    if (!needle || !*needle) return true;
    const std::size_t needleLength = std::strlen(needle);
    if (needleLength > text.size()) return false;
    for (std::size_t start = 0; start + needleLength <= text.size(); ++start) {
        bool match = true;
        for (std::size_t i = 0; i < needleLength; ++i) {
            char left = text[start + i];
            char right = needle[i];
            if (left >= 'A' && left <= 'Z') left = static_cast<char>(left - 'A' + 'a');
            if (right >= 'A' && right <= 'Z') right = static_cast<char>(right - 'A' + 'a');
            if (left != right) { match = false; break; }
        }
        if (match) {
            const bool startsAtBoundary = start == 0 ||
                !IsAsciiAlphaNumeric(text[start - 1]);
            const std::size_t end = start + needleLength;
            const bool endsAtBoundary = end == text.size() ||
                !IsAsciiAlphaNumeric(text[end]);
            if (startsAtBoundary && endsAtBoundary) return true;
        }
    }
    return false;
}

inline std::string FormatContact(const char* charName, int pattern,
                                 const char* outcome) {
    const char* mapped = Lookup(charName, pattern);
    std::string result = mapped && *mapped
        ? std::string(mapped)
        : std::string("#") + std::to_string(pattern);
    if (outcome && *outcome &&
        !ContainsOutcomeCaseInsensitive(result, outcome)) {
        result += " ";
        result += outcome;
    }
    return result;
}

inline std::string FormatSemanticContact(const char* charName, int pattern,
                                         const char* outcome,
                                         int producerMove = -1) {
    const SemanticNote* semantic = LookupSemantic(charName, pattern,
                                                   producerMove);
    if (semantic && !CanOwnRecordedContact(semantic->disposition)) {
        return {};
    }
    const bool resolved = semantic && semantic->label && *semantic->label &&
        HasResolvedContactPresentation(semantic->disposition);
    std::string result = resolved
        ? std::string(semantic->label)
        : std::string("#") + std::to_string(pattern);
    if (outcome && *outcome &&
        !ContainsOutcomeCaseInsensitive(result, outcome)) {
        result += " (";
        result += outcome;
        result += ")";
    }
    return result;
}

inline const SemanticNote* LookupSemanticForGeneratedContactNotation(
    const char* charName, int pattern, const char* outcome, int contacts,
    const std::string& notation) {
    if (!charName || notation.empty()) return nullptr;
    for (int i = 0; i < kGeneratedSemanticNoteCount; ++i) {
        const SemanticNote& note = kGeneratedSemanticNotes[i];
        if (note.pattern != static_cast<std::uint16_t>(pattern) ||
            std::strcmp(note.character, charName) != 0 ||
            !HasResolvedContactPresentation(note.disposition)) {
            continue;
        }
        std::string expected = FormatSemanticContact(
            charName, pattern, outcome, note.producerMove);
        if (contacts > 1) {
            expected += " x";
            expected += std::to_string(contacts);
        }
        if (notation == expected) return &note;
    }
    return nullptr;
}

inline bool IsRawPatternNotation(const std::string& notation, int pattern) {
    const std::string token = std::string("#") + std::to_string(pattern);
    return notation.size() >= token.size() &&
           notation.compare(0, token.size(), token) == 0 &&
           (notation.size() == token.size() ||
            notation[token.size()] == ' ' || notation[token.size()] == '\t');
}

inline bool IsGeneratedContactNotation(const char* charName, int pattern,
                                       const char* outcome, int contacts,
                                       const std::string& notation,
                                       int producerMove = -1) {
    if (notation.empty() || IsRawPatternNotation(notation, pattern)) {
        return true;
    }
    auto withCount = [contacts](std::string value) {
        if (contacts > 1) {
            value += " x";
            value += std::to_string(contacts);
        }
        return value;
    };
    const SemanticNote* semantic = LookupSemantic(charName, pattern,
                                                   producerMove);
    std::string legacySemantic;
    if (semantic && semantic->label && *semantic->label) {
        legacySemantic = semantic->label;
        if (outcome && *outcome &&
            !ContainsOutcomeCaseInsensitive(legacySemantic, outcome)) {
            legacySemantic += " ";
            legacySemantic += outcome;
        }
    }
    std::string sparseLegacySemantic;
    for (int i = 0; i < kLegacySemanticNoteCount; ++i) {
        const SemanticNote& old = kLegacySemanticNotes[i];
        if (old.pattern != static_cast<std::uint16_t>(pattern) ||
            std::strcmp(old.character, charName) != 0 ||
            !old.label || !*old.label) {
            continue;
        }
        sparseLegacySemantic = old.label;
        if (outcome && *outcome &&
            !ContainsOutcomeCaseInsensitive(sparseLegacySemantic, outcome)) {
            sparseLegacySemantic += " ";
            sparseLegacySemantic += outcome;
        }
        break;
    }
    return notation == withCount(FormatContact(charName, pattern, outcome)) ||
           notation == withCount(FormatSemanticContact(
               charName, pattern, outcome, producerMove)) ||
           (!legacySemantic.empty() && notation == withCount(legacySemantic)) ||
           (!sparseLegacySemantic.empty() &&
            notation == withCount(sparseLegacySemantic));
}

}
