#pragma once

#include <cstddef>
#include <cstdint>

#include "Game/BoneSlots.hpp"

namespace OW::SkeletonSemantics {

    enum class Form : uint8_t {
        Unknown,
        Standard,
        DvaMech,
        DvaPilot,
    };

    // Values match the published render slots and Aim/Trigger group masks.
    enum class Slot : size_t {
        Head,
        Neck,
        Chest,
        Pelvis,
        UpperArmLeft,
        UpperArmRight,
        LowerArmLeft,
        LowerArmRight,
        UpperLegLeft,
        UpperLegRight,
        LowerLegLeft,
        LowerLegRight,
        HandLeft,
        HandRight,
        FootLeft,
        FootRight,
        Count,
    };

    enum class CoreAnchorSource : uint8_t {
        Unavailable,
        ObservedBone,
        PositionEstimate,
    };

    using Map = Plexies20260609::RenderSkeletonMap;

    inline constexpr Map kUnavailableMap{
        -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1,
    };

    // Historical UN/Rigel form-specific core IDs. Rigel 373c441a1140d21533ff6c67cab69ae194edc9cd,
    // includes.hpp GetSkel and GetComponent.hpp HeroIDToString agree on the
    // Link +0xD4/+0xD8 low-word predicate. Current live compatibility is unverified.
    // The historical limb layout has no confirmed conversion to the current
    // semantic slots, so these maps expose only the three named core anchors.
    inline constexpr Map kDvaMechCoreMap{
        17, 16, 4, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1,
    };
    inline constexpr Map kDvaPilotCoreMap{
        17, 16, 81, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1,
    };

    inline constexpr size_t SlotIndex(Slot slot) {
        return static_cast<size_t>(slot);
    }

    inline Form ResolveForm(uint64_t heroId, bool linkRead, uint16_t linkId, uint16_t formId) {
        if (!GameData::IsKnownHeroId(heroId))
            return Form::Unknown;
        if (heroId != eHero::HERO_DVA)
            return Form::Standard;
        if (!linkRead)
            return Form::Unknown;
        return linkId != formId ? Form::DvaMech : Form::DvaPilot;
    }

    inline Map RequestedIds(uint64_t heroId, Form form) {
        if (heroId == eHero::HERO_DVA) {
            if (form == Form::DvaMech)
                return kDvaMechCoreMap;
            if (form == Form::DvaPilot)
                return kDvaPilotCoreMap;
            return kUnavailableMap;
        }
        if (form != Form::Standard || !GameData::IsKnownHeroId(heroId))
            return kUnavailableMap;
        return Plexies20260609::ResolveRenderSkeletonMap(heroId);
    }

    inline constexpr const char* FormName(Form form) {
        switch (form) {
        case Form::Standard: return "standard";
        case Form::DvaMech: return "dva_mech";
        case Form::DvaPilot: return "dva_pilot";
        default: return "unknown";
        }
    }

    inline constexpr const char* MappingSource(Form form) {
        switch (form) {
        case Form::Standard: return "plexies_20260609";
        case Form::DvaMech:
        case Form::DvaPilot: return "historical_un_core_only";
        default: return "unavailable";
        }
    }

    inline constexpr bool CanReuseHistory(Form previousForm, uint64_t previousRevision, const Map& previousIds,
                                          Form currentForm, uint64_t currentRevision, const Map& currentIds) {
        return previousForm == currentForm && previousRevision == currentRevision && previousIds == currentIds;
    }

} // namespace OW::SkeletonSemantics
