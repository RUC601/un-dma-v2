#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace OW::EntityMaintenance {
// Cooperative deadline: a started SDK operation is not preemptible.
struct Budget {
    uint64_t started = 0, duration = 1000;
    bool CanStart(uint64_t now) const { return now >= started && now - started < duration; }
};

enum class ColdStage : uint8_t { Identity, Skeleton, TeamName, Skill, LocalSkill, HeroSignal, Stealth, Count, None };
constexpr uint32_t StageBit(ColdStage stage) { return 1u << static_cast<uint8_t>(stage); }
constexpr bool CanObserveHeroSignals(bool controller,bool requested,uint64_t requestedHero,
    uint64_t actorHero,bool alive,bool needsLink,bool link,bool skill) {
    return controller && requested && requestedHero==actorHero && alive && (needsLink?link:skill);
}
constexpr bool CanObserveStealth(bool sombra,bool enemy,bool skill) { return sombra && enemy && skill; }
struct ColdSchedule {
    uint8_t cursor = 0;
    ColdStage Select(uint32_t due) const {
        constexpr auto count = static_cast<uint8_t>(ColdStage::Count);
        for (uint8_t offset = 0; offset < count; ++offset) {
            const auto stage = static_cast<ColdStage>((cursor + offset) % count);
            if (due & StageBit(stage)) return stage;
        }
        return ColdStage::None;
    }
    bool Start(ColdStage selected, ColdStage requested, const Budget& budget, uint64_t now) {
        if (selected != requested || selected == ColdStage::None || !budget.CanStart(now)) return false;
        cursor = (static_cast<uint8_t>(selected) + 1) % static_cast<uint8_t>(ColdStage::Count);
        return true;
    }
};
}
