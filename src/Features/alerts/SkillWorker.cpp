#include "Alerts/Service.hpp"
#include "Alerts/SkillDetector.hpp"
#include "Game/Overwatch.hpp"
#include "Game/snapshots/SnapshotAccess.hpp"
#include "Utils/ProcessConnection.hpp"

namespace V2::Alerts {
namespace {
SkillObservation Observation(const OW::c_entity& entity, const OW::c_entity& local, uint32_t now, uint8_t slot) {
    const auto epoch = OW::ProcessConnection::ConnectionEpoch(), scene = OW::EntitySceneGeneration();
    const bool ultimate = slot == kUltimateSlot;
    const bool validUltimate = std::isfinite(entity.ultimate) && entity.ultimate >= 0 && entity.ultimate <= 100.01f;
    return {entity.address, entity.actor_generation, entity.HeroID, entity.connection_epoch, entity.scene_generation,
        ultimate ? entity.display_ultimate_tick : entity.skill_active_tick_ms[slot],
        ultimate ? entity.ultimate >= 100 : slot == 0 ? entity.skill1act : entity.skill2act,
        (!ultimate || validUltimate) && entity.Team && entity.team_known && local.team_known &&
        local.connection_epoch == epoch && local.scene_generation == scene && entity.address != local.address &&
        entity.connection_epoch == epoch && entity.scene_generation == scene &&
        entity.HasUsableBody(now, scene), slot};
}
bool Current(const SkillObservation& source) {
    if (!OW::RuntimeWorkersRunning() || !OW::ProcessConnection::IsConnected() ||
        OW::ProcessConnection::ConnectionEpoch() != source.epoch || OW::EntitySceneGeneration() != source.scene) return false;
    const auto frame = OW::SnapshotFrame();
    const uint32_t now = GetTickCount();
    for (const auto& entity : frame->entities) {
        if (entity.address != source.body || entity.actor_generation != source.actor || entity.HeroID != source.hero) continue;
        const auto sample = Observation(entity, frame->local, now, source.slot);
        return sample.eligible && sample.tick && uint32_t(now-sample.tick) <= 450 &&
            (source.slot != kUltimateSlot || sample.active);
    }
    return false;
}
}
void SkillWorker() {
    SkillDetector detector;
    uint64_t previousHero=0,previousActor=0;
    while (OW::RuntimeWorkersRunning()) {
        if (!OW::ProcessConnection::IsConnected()) {detector.Reset();SetRuntimeHero(0);}
        else {
            const auto frame = OW::SnapshotFrame();
            if(previousHero!=frame->local.HeroID||previousActor!=frame->local.actor_generation) {
                detector.Reset();previousHero=frame->local.HeroID;previousActor=frame->local.actor_generation;
            }
            SetRuntimeHero(frame->local.HeroID);
            const auto now = GetTickCount();
            std::vector<SkillObservation> samples;
            const auto settings = GetRuntimeSettings();
            for (const auto& entity : frame->entities) {
                if (settings.skillEnabled) {
                    for (size_t i = 0; i < kDefinitions.size(); ++i)
                        if (kDefinitions[i].hero == entity.HeroID && settings.rules[i].enabled)
                            samples.push_back(Observation(entity, frame->local, now, kDefinitions[i].slot));
                }
                if (ObserveUltimateHero(entity.HeroID))
                    samples.push_back(Observation(entity, frame->local, now, kUltimateSlot));
            }
            for (const auto& event : detector.Observe(samples, now)) {
                auto current = [source=event.source,localHero=frame->local.HeroID,localActor=frame->local.actor_generation] {
                    const auto frame=OW::SnapshotFrame();
                    return frame->local.HeroID==localHero && frame->local.actor_generation==localActor && Current(source);
                };
                if (event.ruleId) Submit(event.ruleId, current);
                else SubmitUltimate(event.source.hero, current);
            }
        }
        OW::RuntimeWorkerSleep(16);
    }
}
}
