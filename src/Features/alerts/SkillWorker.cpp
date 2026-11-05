#include "Alerts/Service.hpp"
#include "Alerts/SkillDetector.hpp"
#include "Game/Overwatch.hpp"
#include "Game/snapshots/SnapshotAccess.hpp"
#include "Utils/ProcessConnection.hpp"

namespace V2::Alerts {
namespace {
SkillObservation Observation(const OW::c_entity& entity, const OW::c_entity& local, uint32_t now) {
    const auto epoch = OW::ProcessConnection::ConnectionEpoch(), scene = OW::EntitySceneGeneration();
    return {entity.address, entity.actor_generation, entity.HeroID, entity.connection_epoch, entity.scene_generation,
        entity.skill_active_tick_ms[0], entity.skill1act,
        SkillForHero(entity.HeroID) && entity.Team && entity.team_known && local.team_known &&
        local.connection_epoch == epoch && local.scene_generation == scene && entity.address != local.address &&
        entity.connection_epoch == epoch && entity.scene_generation == scene &&
        entity.HasUsableBody(now, scene)};
}
bool Current(const SkillObservation& source) {
    if (!OW::RuntimeWorkersRunning() || !OW::ProcessConnection::IsConnected() ||
        OW::ProcessConnection::ConnectionEpoch() != source.epoch || OW::EntitySceneGeneration() != source.scene) return false;
    const auto frame = OW::SnapshotFrame();
    const uint32_t now = GetTickCount();
    for (const auto& entity : frame->entities) {
        if (entity.address != source.body || entity.actor_generation != source.actor || entity.HeroID != source.hero) continue;
        const auto sample = Observation(entity, frame->local, now);
        return sample.eligible && sample.tick && uint32_t(now-sample.tick) <= 450;
    }
    return false;
}
}
void SkillWorker() {
    SkillDetector detector;
    while (OW::RuntimeWorkersRunning()) {
        if (!OW::ProcessConnection::IsConnected()) detector.Reset();
        else {
            const auto frame = OW::SnapshotFrame();
            const auto now = GetTickCount();
            std::vector<SkillObservation> samples;
            for (const auto& entity : frame->entities)
                if (ObserveSkillHero(entity.HeroID)) samples.push_back(Observation(entity, frame->local, now));
            for (const auto& event : detector.Observe(samples, now))
                Submit(event.ruleId, [source = event.source] { return Current(source); });
        }
        OW::RuntimeWorkerSleep(16);
    }
}
}
