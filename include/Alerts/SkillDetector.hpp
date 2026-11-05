#pragma once
#include "Alerts/Settings.hpp"
#include <span>
#include <unordered_map>

namespace V2::Alerts {
struct SkillObservation {
    uint64_t body = 0, actor = 0, hero = 0, epoch = 0, scene = 0;
    uint32_t tick = 0;
    bool active = false;
    bool eligible = false;
};
struct SkillEvent {
    const char* ruleId = nullptr;
    SkillObservation source;
};
class SkillDetector {
    struct Track { SkillObservation last; uint32_t lastCast = 0; uint64_t seen = 0; };
    std::unordered_map<uint64_t, Track> tracks_;
    uint64_t sequence_ = 0;
public:
    void Reset() { tracks_.clear(); }
    std::vector<SkillEvent> Observe(std::span<const SkillObservation> observations, uint32_t now) {
        std::vector<SkillEvent> events;
        ++sequence_;
        for (const auto& sample : observations) {
            const auto* definition = SkillForHero(sample.hero);
            if (!sample.eligible || !definition || !sample.body || !sample.actor ||
                !sample.epoch || !sample.scene || !sample.tick || uint32_t(now-sample.tick) > 450) continue;
            auto& track = tracks_[sample.body];
            const auto previous = track.last;
            const bool sameActor = previous.actor == sample.actor && previous.hero == sample.hero &&
                previous.epoch == sample.epoch && previous.scene == sample.scene;
            if (sameActor && previous.tick && int32_t(sample.tick-previous.tick) < 0) {
                track.seen = sequence_;
                continue;
            }
            const bool continuous = previous.tick && sameActor &&
                uint32_t(sample.tick-previous.tick) <= 450;
            if (!continuous) track.lastCast = 0;
            // Initial active=true is a baseline, never an inferred cast. Ignore repeated cached samples.
            if (continuous && sample.tick != previous.tick && !previous.active && sample.active &&
                (!track.lastCast || uint32_t(sample.tick-track.lastCast) >= 800)) {
                events.push_back({definition->id, sample});
                track.lastCast = sample.tick;
            }
            track.last = sample;
            track.seen = sequence_;
        }
        std::erase_if(tracks_, [&](const auto& item) { return item.second.seen != sequence_; });
        return events;
    }
};
}
