#pragma once

#include <cstddef>
#include <cstdint>

namespace Mission::EntityContactPhaseBridgePolicy {

// Literal resource + sampled-prior + sampled-current + exact-contact tuples, not a family graph.
// Callers must still prove slot, generation, sample, action boundary and a complete journal.
enum class BridgeKind : std::uint8_t {
    None = 0,

    DirectPostContactRetirement,

    CollapsedIntermediateContact,

    CollapsedControllerToRecovery = CollapsedIntermediateContact,
};

struct Rule {
    const char* resource;
    std::uint16_t sampledPriorPattern;
    std::uint16_t sampledCurrentPattern;
    std::uint16_t exactContactPattern;
    BridgeKind kind;
};

inline constexpr Rule kRules[] = {
    {"akane", 400, 404, 400, BridgeKind::DirectPostContactRetirement},
    {"akane", 401, 404, 401, BridgeKind::DirectPostContactRetirement},
    {"akane", 402, 403, 402, BridgeKind::DirectPostContactRetirement},
    {"akane", 402, 404, 402, BridgeKind::DirectPostContactRetirement},
    {"akane", 403, 404, 403, BridgeKind::DirectPostContactRetirement},

    {"akiko", 404, 401, 404, BridgeKind::DirectPostContactRetirement},
    {"akiko", 406, 405, 406, BridgeKind::DirectPostContactRetirement},
    {"akiko", 407, 405, 407, BridgeKind::DirectPostContactRetirement},
    {"akiko", 408, 409, 408, BridgeKind::DirectPostContactRetirement},

    {"ayu", 409, 410, 409, BridgeKind::DirectPostContactRetirement},

    {"ikumi", 420, 430, 420, BridgeKind::DirectPostContactRetirement},
    {"ikumi", 421, 430, 421, BridgeKind::DirectPostContactRetirement},
    {"ikumi", 422, 430, 422, BridgeKind::DirectPostContactRetirement},
    {"ikumi2", 420, 430, 420, BridgeKind::DirectPostContactRetirement},
    {"ikumi2", 421, 430, 421, BridgeKind::DirectPostContactRetirement},
    {"ikumi2", 422, 430, 422, BridgeKind::DirectPostContactRetirement},

    {"kanna", 406, 409, 406, BridgeKind::DirectPostContactRetirement},
    {"kanna", 430, 431, 430, BridgeKind::DirectPostContactRetirement},

    {"kano", 412, 413, 412, BridgeKind::DirectPostContactRetirement},
    {"kano", 419, 420, 419, BridgeKind::DirectPostContactRetirement},

    {"mai", 429, 401, 429, BridgeKind::DirectPostContactRetirement},
    {"mai", 430, 401, 430, BridgeKind::DirectPostContactRetirement},
    {"mai", 431, 402, 431, BridgeKind::DirectPostContactRetirement},
    {"mai", 451, 402, 451, BridgeKind::DirectPostContactRetirement},

    {"minagi", 404, 425, 404, BridgeKind::DirectPostContactRetirement},
    {"minagi", 405, 425, 405, BridgeKind::DirectPostContactRetirement},
    {"minagi", 406, 425, 406, BridgeKind::DirectPostContactRetirement},
    {"minagi", 422, 400, 422, BridgeKind::DirectPostContactRetirement},
    {"minagi", 423, 400, 423, BridgeKind::DirectPostContactRetirement},
    {"minagi", 424, 400, 424, BridgeKind::DirectPostContactRetirement},
    {"minagi", 407, 400, 407, BridgeKind::DirectPostContactRetirement},
    {"minagi", 408, 400, 408, BridgeKind::DirectPostContactRetirement},
    {"minagi", 409, 400, 409, BridgeKind::DirectPostContactRetirement},
    {"minagi", 410, 400, 410, BridgeKind::DirectPostContactRetirement},
    {"minagi", 411, 400, 411, BridgeKind::DirectPostContactRetirement},
    {"minagi", 412, 400, 412, BridgeKind::DirectPostContactRetirement},
    {"minagi", 426, 400, 426, BridgeKind::DirectPostContactRetirement},
    {"minagi", 427, 400, 427, BridgeKind::DirectPostContactRetirement},
    {"minagi", 428, 400, 428, BridgeKind::DirectPostContactRetirement},
    {"minagi", 466, 400, 466, BridgeKind::DirectPostContactRetirement},
    {"minagi", 467, 400, 467, BridgeKind::DirectPostContactRetirement},
    {"minagi", 468, 400, 468, BridgeKind::DirectPostContactRetirement},
    {"minagi", 416, 400, 416, BridgeKind::DirectPostContactRetirement},
    {"minagi", 418, 400, 418, BridgeKind::DirectPostContactRetirement},
    {"minagi", 420, 400, 420, BridgeKind::DirectPostContactRetirement},
    {"minagi", 417, 400, 417, BridgeKind::DirectPostContactRetirement},
    {"minagi", 419, 400, 419, BridgeKind::DirectPostContactRetirement},
    {"minagi", 421, 400, 421, BridgeKind::DirectPostContactRetirement},

    // Michiru attack PATs that can be entered and retired between endpoint samples.
    {"minagi", 400, 425, 404, BridgeKind::CollapsedIntermediateContact},
    {"minagi", 400, 425, 405, BridgeKind::CollapsedIntermediateContact},
    {"minagi", 400, 425, 406, BridgeKind::CollapsedIntermediateContact},
    {"minagi", 425, 400, 404, BridgeKind::CollapsedIntermediateContact},
    {"minagi", 425, 400, 405, BridgeKind::CollapsedIntermediateContact},
    {"minagi", 425, 400, 406, BridgeKind::CollapsedIntermediateContact},

    {"misaki", 406, 410, 406, BridgeKind::DirectPostContactRetirement},
    {"misaki", 407, 410, 407, BridgeKind::DirectPostContactRetirement},

    {"misuzu", 453, 424, 453, BridgeKind::DirectPostContactRetirement},
    {"misuzu", 454, 424, 454, BridgeKind::DirectPostContactRetirement},
    {"misuzu", 455, 424, 455, BridgeKind::DirectPostContactRetirement},

    {"mizukab", 417, 420, 417, BridgeKind::DirectPostContactRetirement},
    {"mizukab", 418, 420, 418, BridgeKind::DirectPostContactRetirement},
    {"mizukab", 464, 465, 464, BridgeKind::DirectPostContactRetirement},
    {"mizukab", 466, 467, 466, BridgeKind::DirectPostContactRetirement},
    {"mizukab", 469, 470, 469, BridgeKind::DirectPostContactRetirement},

    {"nanase", 400, 403, 400, BridgeKind::DirectPostContactRetirement},
    {"nanase", 413, 403, 413, BridgeKind::DirectPostContactRetirement},
    {"nanase", 419, 424, 419, BridgeKind::DirectPostContactRetirement},
    {"nanase", 420, 424, 420, BridgeKind::DirectPostContactRetirement},
    {"nanase", 428, 424, 428, BridgeKind::DirectPostContactRetirement},
    {"nanase2", 400, 403, 400, BridgeKind::DirectPostContactRetirement},
    {"nanase2", 413, 403, 413, BridgeKind::DirectPostContactRetirement},
    {"nanase2", 419, 424, 419, BridgeKind::DirectPostContactRetirement},
    {"nanase2", 420, 424, 420, BridgeKind::DirectPostContactRetirement},
    {"nanase2", 428, 424, 428, BridgeKind::DirectPostContactRetirement},

    {"nayukib", 404, 406, 404, BridgeKind::DirectPostContactRetirement},
    {"nayukib", 405, 407, 405, BridgeKind::DirectPostContactRetirement},
    {"nayukib", 408, 406, 404, BridgeKind::CollapsedIntermediateContact},
    {"nayukib", 409, 407, 405, BridgeKind::CollapsedIntermediateContact},

    // Shiori's fan can update repeatedly before sampling; only #441/#444 are accepted contact phases.
    {"shiori", 441, 443, 441, BridgeKind::DirectPostContactRetirement},
    {"shiori", 441, 444, 441, BridgeKind::DirectPostContactRetirement},
    {"shiori", 444, 442, 444, BridgeKind::DirectPostContactRetirement},
    {"shiori", 440, 443, 441, BridgeKind::CollapsedIntermediateContact},
    {"shiori", 440, 444, 441, BridgeKind::CollapsedIntermediateContact},
};

inline constexpr std::size_t kRuleCount = sizeof(kRules) / sizeof(kRules[0]);

constexpr bool ResourceEquals(const char* left, const char* right) {
    if (!left || !right) return false;
    for (std::size_t i = 0;; ++i) {
        if (left[i] != right[i]) return false;
        if (left[i] == '\0') return true;
    }
}

constexpr BridgeKind ClassifyBridge(const char* resource,
                                    std::uint16_t sampledPriorPattern,
                                    std::uint16_t sampledCurrentPattern,
                                    std::uint16_t exactContactPattern) {
    for (std::size_t i = 0; i < kRuleCount; ++i) {
        const Rule& rule = kRules[i];
        if (ResourceEquals(resource, rule.resource) &&
            sampledPriorPattern == rule.sampledPriorPattern &&
            sampledCurrentPattern == rule.sampledCurrentPattern &&
            exactContactPattern == rule.exactContactPattern) {
            return rule.kind;
        }
    }
    return BridgeKind::None;
}

// A bridge proves which PAT owns the contact, not that the destination is disposable: only a
// catalog-confirmed recovery endpoint may be omitted.
constexpr bool CanSuppressSampledEndpointObjective(
    BridgeKind bridge,
    bool endpointPromisesContact,
    bool endpointIsPostContactRecovery) {
    return bridge != BridgeKind::None &&
           !endpointPromisesContact &&
           endpointIsPostContactRecovery;
}

constexpr bool HasDuplicateRuleKeys() {
    for (std::size_t i = 0; i < kRuleCount; ++i) {
        for (std::size_t j = i + 1; j < kRuleCount; ++j) {
            if (ResourceEquals(kRules[i].resource, kRules[j].resource) &&
                kRules[i].sampledPriorPattern ==
                    kRules[j].sampledPriorPattern &&
                kRules[i].sampledCurrentPattern ==
                    kRules[j].sampledCurrentPattern &&
                kRules[i].exactContactPattern ==
                    kRules[j].exactContactPattern) {
                return true;
            }
        }
    }
    return false;
}

static_assert(!HasDuplicateRuleKeys(),
              "entity contact phase bridge keys must be unique");
static_assert(ClassifyBridge("nayukib", 404, 406, 404) ==
                  BridgeKind::DirectPostContactRetirement &&
              ClassifyBridge("nayukib", 408, 406, 404) ==
                  BridgeKind::CollapsedIntermediateContact,
              "the original Snowbunny bridge must remain available");
static_assert(ClassifyBridge("nayuki", 408, 406, 404) == BridgeKind::None &&
              ClassifyBridge("nayukib", 408, 407, 404) == BridgeKind::None,
              "resource and literal transition identity must fail closed");
static_assert(CanSuppressSampledEndpointObjective(
                  BridgeKind::DirectPostContactRetirement, false, true) &&
              !CanSuppressSampledEndpointObjective(
                  BridgeKind::DirectPostContactRetirement, true, true) &&
              !CanSuppressSampledEndpointObjective(
                  BridgeKind::None, false, true),
              "only a bridged, non-contact recovery endpoint is suppressible");

}
