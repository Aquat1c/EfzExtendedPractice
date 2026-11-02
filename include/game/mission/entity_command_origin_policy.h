#pragma once

#include <cstddef>
#include <cstdint>

namespace Mission::EntityCommandOriginPolicy {

struct Origin {
    const char* resource;
    std::uint16_t rootPattern;
    std::uint16_t activationPattern;
    std::uint16_t contactPattern;
    const char* notation;
    std::uint8_t attackMask;
};

inline constexpr std::uint8_t kAttackButtonD = 0x80;
inline constexpr int kEntityRingSlotCapacity = 64;

inline constexpr Origin kOrigins[] = {
    {"mai", 401, 431, 431, "(J.)236S", kAttackButtonD},
    {"mai", 401, 451, 451, "(J.)214S", kAttackButtonD},
    {"mai", 401, 436, 454, "(J.)412S", kAttackButtonD},
    {"mai", 401, 414, 414, "(J.)22S",  kAttackButtonD},
};

inline constexpr std::size_t kOriginCount =
    sizeof(kOrigins) / sizeof(kOrigins[0]);

constexpr bool ResourceEquals(const char* left, const char* right) {
    if (!left || !right) return false;
    for (std::size_t index = 0;; ++index) {
        if (left[index] != right[index]) return false;
        if (left[index] == '\0') return true;
    }
}

constexpr const Origin* FindByTransition(const char* resource,
                                         std::uint16_t rootPattern,
                                         std::uint16_t activationPattern) {
    for (std::size_t index = 0; index < kOriginCount; ++index) {
        const Origin& origin = kOrigins[index];
        if (ResourceEquals(resource, origin.resource) &&
            rootPattern == origin.rootPattern &&
            activationPattern == origin.activationPattern) {
            return &origin;
        }
    }
    return nullptr;
}

constexpr const Origin* FindByContact(const char* resource,
                                      std::uint16_t contactPattern) {
    for (std::size_t index = 0; index < kOriginCount; ++index) {
        const Origin& origin = kOrigins[index];
        if (ResourceEquals(resource, origin.resource) &&
            contactPattern == origin.contactPattern) {
            return &origin;
        }
    }
    return nullptr;
}

constexpr const Origin* ValidateBoundCommand(const char* resource,
                                             int slot,
                                             int generation,
                                             int rootPattern,
                                             int activationPattern,
                                             int expectedAttackMask) {
    if (slot < 0 || slot >= kEntityRingSlotCapacity || generation <= 0 ||
        rootPattern <= 0 || rootPattern > 0xFFFF || activationPattern <= 0 ||
        activationPattern > 0xFFFF || expectedAttackMask <= 0 ||
        expectedAttackMask > 0xFF) {
        return nullptr;
    }
    const Origin* origin = FindByTransition(
        resource, static_cast<std::uint16_t>(rootPattern),
        static_cast<std::uint16_t>(activationPattern));
    return origin && origin->attackMask ==
                         static_cast<std::uint8_t>(expectedAttackMask)
        ? origin
        : nullptr;
}

constexpr bool LifecycleObjectiveBindsCommand(
    const Origin* origin,
    int commandSlot, int commandGeneration,
    int objectiveSlot, int objectiveGeneration,
    bool objectiveIsMorph,
    int objectivePattern, int objectivePriorPattern) {
    return origin && commandSlot >= 0 &&
           commandSlot < kEntityRingSlotCapacity && commandGeneration > 0 &&
           objectiveIsMorph && objectiveSlot == commandSlot &&
           objectiveGeneration == commandGeneration &&
           objectivePattern == origin->activationPattern &&
           objectivePriorPattern == origin->rootPattern;
}

constexpr bool ContactObjectiveBindsCommand(
    const Origin* origin,
    int commandSlot, int commandGeneration,
    int objectiveSlot, int objectiveGeneration,
    bool hasOneExactContactPattern, int objectiveContactPattern,
    bool producerIsMorph, bool producerIsSpawn,
    int producerPattern, int producerPriorPattern) {
    if (!origin || commandSlot < 0 ||
        commandSlot >= kEntityRingSlotCapacity || commandGeneration <= 0 ||
        !hasOneExactContactPattern ||
        objectiveContactPattern != origin->contactPattern) {
        return false;
    }
    if (origin->contactPattern == origin->activationPattern) {
        return objectiveSlot == commandSlot &&
               objectiveGeneration == commandGeneration &&
               producerIsMorph &&
               producerPattern == origin->activationPattern &&
               producerPriorPattern == origin->rootPattern;
    }
    // Sole curated cross-slot relationship: the bound #401 -> #436 controller emits a #454 hit child.
    // Do not generalize from timing, adjacency, or family labels.
    return origin->rootPattern == 401 &&
           origin->activationPattern == 436 &&
           origin->contactPattern == 454 && producerIsSpawn &&
           producerPattern == 454 && producerPriorPattern < 0 &&
           objectiveSlot >= 0 &&
           objectiveSlot < kEntityRingSlotCapacity &&
           objectiveGeneration > 0;
}

constexpr bool ContactBelongsToBoundCommand(const Origin* origin,
                                             int contactPattern) {
    return origin && contactPattern > 0 && contactPattern <= 0xFFFF &&
           origin->contactPattern ==
               static_cast<std::uint16_t>(contactPattern);
}

constexpr bool RuntimeTransitionMatches(const Origin* origin,
                                        int requiredSlot,
                                        int requiredGeneration,
                                        int observedSlot,
                                        int observedGeneration,
                                        int observedPriorPattern,
                                        int observedPattern) {
    return origin && requiredSlot == observedSlot && requiredGeneration > 0 &&
           requiredGeneration == observedGeneration &&
           observedPriorPattern == origin->rootPattern &&
           observedPattern == origin->activationPattern;
}

static_assert(FindByTransition("mai", 401, 436) != nullptr &&
              FindByContact("mai", 454) != nullptr &&
              FindByTransition("mai", 401, 454) == nullptr,
              "Mini-Mai Blitz keeps its controller and child distinct");
static_assert(ValidateBoundCommand("mai", 7, 1, 401, 431,
                                  kAttackButtonD) != nullptr &&
              ContactBelongsToBoundCommand(
                  ValidateBoundCommand("mai", 7, 1, 401, 436,
                                       kAttackButtonD),
                  454),
              "a bound Mini-Mai command retains input and contact identity");
static_assert(!ValidateBoundCommand("mai", kEntityRingSlotCapacity, 1,
                                   401, 431, kAttackButtonD),
              "a command cannot bind outside the restored entity ring");

}
