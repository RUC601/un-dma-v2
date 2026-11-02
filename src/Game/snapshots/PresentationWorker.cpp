#include "Interpolation.hpp"
#include "PresentationWorker.hpp"
#include "Game/Overwatch.hpp"

inline std::chrono::microseconds PresentPeriod(DWORD hz) {
    hz = std::clamp<DWORD>(hz, 1, 240);
    return std::chrono::microseconds((std::max<DWORD>)(1, 1000000 / hz));
}

inline uint32_t TickAgeMs(uint32_t now, uint32_t then) {
    if (then == 0 || now < then)
        return 0;
    return now - then;
}

inline void AddPresentSourceCount(Diagnostics::PresentSourceCounts& counts,
                                  OW::PresentSnapshotSource source,
                                  uint64_t amount = 1) {
    switch (source) {
    case OW::PresentSnapshotSource::Raw:
        counts.raw += amount;
        break;
    case OW::PresentSnapshotSource::Interp:
        counts.interp += amount;
        break;
    case OW::PresentSnapshotSource::Extrap:
        counts.extrap += amount;
        break;
    case OW::PresentSnapshotSource::Hold:
        counts.hold += amount;
        break;
    default:
        break;
    }
}

inline uint32_t EntityRawSampleTickMs(const OW::c_entity& entity) {
    if (entity.render_sample_tick_ms != 0)
        return entity.render_sample_tick_ms;
    return entity.position_sample_tick_ms;
}

struct RawPresentEntityTrack {
    OW::c_entity previous{};
    OW::c_entity current{};
    bool hasPrevious = false;
    bool hasCurrent = false;
};

inline uint64_t PresentEntityTrackKey(const OW::c_entity& entity) {
    return entity.roster_key != 0 ? entity.roster_key : entity.address;
}

inline bool PresentEntityContinuityCompatible(const OW::c_entity& previous,
                                              const OW::c_entity& current) {
    if (!previous.address || !current.address || previous.address != current.address)
        return false;
    if (previous.roster_state != OW::EntityRosterState::Fresh ||
        current.roster_state != OW::EntityRosterState::Fresh) {
        return false;
    }
    if (!previous.Alive || !current.Alive)
        return false;
    if (previous.roster_key != 0 && current.roster_key != 0 &&
        previous.roster_key != current.roster_key) {
        return false;
    }
    if (previous.match_id != 0 && current.match_id != 0 &&
        previous.match_id != current.match_id) {
        return false;
    }
    if (previous.HeroID != 0 && current.HeroID != 0 &&
        previous.HeroID != current.HeroID) {
        return false;
    }

    const uint32_t previousTick = EntityRawSampleTickMs(previous);
    const uint32_t currentTick = EntityRawSampleTickMs(current);
    if (previousTick == 0 || currentTick == 0 || currentTick <= previousTick)
        return false;

    const uint32_t intervalMs = currentTick - previousTick;
    return intervalMs >= 4 && intervalMs <= 250;
}

inline void AttachPresentPreviousRawSample(OW::c_entity& entity,
                                           const OW::c_entity& previous) {
    entity.previous_render_sample_tick_ms = EntityRawSampleTickMs(previous);
    entity.render_sample_tick_ms = EntityRawSampleTickMs(entity);
    entity.has_previous_render_sample = entity.previous_render_sample_tick_ms != 0 &&
        entity.render_sample_tick_ms > entity.previous_render_sample_tick_ms;

    entity.previous_head_pos = previous.head_pos;
    entity.previous_velocity = previous.velocity;
    entity.previous_pos = previous.pos;
    entity.previous_neck_pos = previous.neck_pos;
    entity.previous_chest_pos = previous.chest_pos;
    entity.previous_skeleton_bones = previous.skeleton_bones;
    entity.previous_skeleton_bone_valid = previous.skeleton_bone_valid;
    entity.previous_cached_bot_chest_bone = previous.cached_bot_chest_bone;
    entity.previous_cached_bot_chest_bone_valid = previous.cached_bot_chest_bone_valid;
}

inline OW::c_entity PreparePresentEntitySample(const OW::c_entity& source,
                                               const OW::c_entity* previousRaw) {
    OW::c_entity entity = source;
    entity.render_sample_tick_ms = EntityRawSampleTickMs(source);
    entity.has_previous_render_sample = false;

    if (previousRaw && PresentEntityContinuityCompatible(*previousRaw, entity))
        AttachPresentPreviousRawSample(entity, *previousRaw);

    return entity;
}

inline float PresentEntityInterpolationAlpha(const OW::c_entity& entity,
                                             uint32_t now,
                                             uint32_t delayMs) {
    if (!OverlayRenderDetail::CanInterpolateEntity(entity))
        return 1.0f;

    const uint32_t previousTick = entity.previous_render_sample_tick_ms;
    const uint32_t currentTick = entity.render_sample_tick_ms;
    if (previousTick == 0 || currentTick == 0 || currentTick <= previousTick)
        return 1.0f;

    const uint32_t intervalMs = currentTick - previousTick;
    if (intervalMs < 4 || intervalMs > 250)
        return 1.0f;

    if (TickAgeMs(now, currentTick) > 250)
        return 1.0f;

    const uint32_t sampleTick = now > delayMs ? now - delayMs : now;
    if (sampleTick <= previousTick)
        return 0.0f;
    if (sampleTick >= currentTick)
        return 1.0f;

    return static_cast<float>(sampleTick - previousTick) / static_cast<float>(intervalMs);
}

inline void SetPresentEntityMeta(OW::c_entity& entity,
                                 OW::PresentSnapshotSource source,
                                 uint32_t rawTick,
                                 uint32_t now,
                                 uint64_t nowUs,
                                 uint32_t ageMs,
                                 float predictionMs,
                                 float confidence) {
    entity.present_source = source;
    entity.present_raw_sample_tick_ms = rawTick;
    entity.present_tick_ms = now;
    entity.present_tick_us = nowUs;
    entity.present_age_ms = ageMs;
    entity.present_prediction_ms = predictionMs;
    entity.present_confidence = std::clamp(confidence, 0.0f, 1.0f);
}

inline bool ApplyPresentEntityExtrapolation(OW::c_entity& entity,
                                            uint32_t now,
                                            uint32_t maxExtrapMs,
                                            float& predictionMs) {
    const uint32_t rawTick = EntityRawSampleTickMs(entity);
    if (rawTick == 0 || now <= rawTick || maxExtrapMs == 0)
        return false;

    const uint32_t ageMs = now - rawTick;
    if (ageMs > maxExtrapMs)
        return false;

    const OW::Motion::EntityMotionEstimate motion = OW::Motion::EstimateEntityMotion(entity);
    if (!motion.valid)
        return false;

    float speed = 0.0f;
    if (!OverlayRenderDetail::IsReasonableRenderVelocity(motion.effectiveVelocity, &speed))
        return false;

    OW::Vector3 offset = motion.effectiveVelocity * (static_cast<float>(ageMs) / 1000.0f);
    float offsetSize = offset.Size();
    if (!std::isfinite(offsetSize) || offsetSize < 0.001f)
        return false;

    constexpr float kMaxPresentOffsetMeters = 0.45f;
    if (offsetSize > kMaxPresentOffsetMeters) {
        const float scale = kMaxPresentOffsetMeters / offsetSize;
        offset = offset * scale;
        offsetSize = kMaxPresentOffsetMeters;
    }

    OverlayRenderDetail::OffsetRenderPoint(entity.pos, offset);
    OverlayRenderDetail::OffsetRenderPoint(entity.head_pos, offset);
    OverlayRenderDetail::OffsetRenderPoint(entity.neck_pos, offset);
    OverlayRenderDetail::OffsetRenderPoint(entity.chest_pos, offset);
    if (entity.cached_bot_chest_bone_valid)
        OverlayRenderDetail::OffsetRenderPoint(entity.cached_bot_chest_bone, offset);

    for (size_t i = 0; i < entity.skeleton_bones.size(); ++i) {
        if (entity.skeleton_bone_valid[i])
            OverlayRenderDetail::OffsetRenderPoint(entity.skeleton_bones[i], offset);
    }

    predictionMs = static_cast<float>(ageMs);
    return true;
}

inline OW::c_entity BuildPresentEntity(const OW::c_entity& rawSource,
                                       const OW::c_entity* previousRaw,
                                       uint32_t now,
                                       uint64_t nowUs,
                                       uint32_t delayMs) {
    const OW::c_entity source = PreparePresentEntitySample(rawSource, previousRaw);
    OW::c_entity entity = source;
    const uint32_t rawTick = EntityRawSampleTickMs(source);
    const uint32_t ageMs = TickAgeMs(now, rawTick);
    const uint32_t maxExtrapMs = OW::PresentMaxExtrapolationMs();
    auto setMeta = [&](OW::PresentSnapshotSource sourceKind,
                       float predictionMs,
                       float confidence) {
        SetPresentEntityMeta(entity, sourceKind, rawTick, now, nowUs, ageMs, predictionMs, confidence);
    };

    if (!source.HasUsableBody(GetTickCount(),OW::EntitySceneGeneration()) || !source.address) {
        setMeta(OW::PresentSnapshotSource::Hold, 0.0f, 0.25f);
        return entity;
    }

    if (rawTick == 0) {
        setMeta(OW::PresentSnapshotSource::Hold, 0.0f, 0.35f);
        return entity;
    }

    if (!source.has_previous_render_sample) {
        const OW::PresentSnapshotSource sourceKind =
            ageMs <= maxExtrapMs ? OW::PresentSnapshotSource::Raw : OW::PresentSnapshotSource::Hold;
        setMeta(sourceKind, 0.0f, ageMs <= maxExtrapMs ? 1.0f : 0.55f);
        return entity;
    }

    if (!OverlayRenderDetail::CanInterpolateEntity(source)) {
        setMeta(OW::PresentSnapshotSource::Hold, 0.0f, 0.55f);
        return entity;
    }

    const float alpha = PresentEntityInterpolationAlpha(source, now, delayMs);
    if (alpha < 0.999f) {
        entity = OverlayRenderDetail::InterpolateEntitySamples(source, alpha);
        setMeta(OW::PresentSnapshotSource::Interp, 0.0f, 0.90f);
        return entity;
    }

    if (ageMs == 0) {
        setMeta(OW::PresentSnapshotSource::Raw, 0.0f, 1.0f);
        return entity;
    }

    float predictionMs = 0.0f;
    if (ApplyPresentEntityExtrapolation(entity, now, maxExtrapMs, predictionMs)) {
        const float confidence = std::clamp(1.0f - (static_cast<float>(ageMs) /
            static_cast<float>((std::max<uint32_t>)(1, maxExtrapMs * 4))), 0.75f, 1.0f);
        setMeta(OW::PresentSnapshotSource::Extrap, predictionMs, confidence);
        return entity;
    }

    const OW::PresentSnapshotSource sourceKind =
        ageMs <= maxExtrapMs ? OW::PresentSnapshotSource::Raw : OW::PresentSnapshotSource::Hold;
    setMeta(sourceKind, 0.0f, ageMs <= maxExtrapMs ? 0.85f : 0.55f);
    return entity;
}

struct PresentEntityBuildResult {
    std::vector<OW::c_entity> entities;
    Diagnostics::PresentSourceCounts counts{};
    double predictionSumMs = 0.0;
    double predictionMaxMs = 0.0;
    uint64_t predictionSamples = 0;
};

inline void UpdatePresentEntityTracks(const std::vector<OW::c_entity>& rawEntities,
                                      std::unordered_map<uint64_t, RawPresentEntityTrack>& rawEntityTracks) {
    std::unordered_set<uint64_t> seenKeys;
    for (const OW::c_entity& source : rawEntities) {
        const uint64_t key = PresentEntityTrackKey(source);
        if (key == 0)
            continue;

        seenKeys.insert(key);
        RawPresentEntityTrack& track = rawEntityTracks[key];
        const uint32_t sourceTick = EntityRawSampleTickMs(source);
        const uint32_t trackTick = track.hasCurrent ? EntityRawSampleTickMs(track.current) : 0;
        const bool sameTrack =
            track.hasCurrent &&
            PresentEntityTrackKey(track.current) == key &&
            (track.current.address == source.address || !track.current.address || !source.address);

        if (!sameTrack || sourceTick == 0) {
            track.current = source;
            track.hasCurrent = true;
            track.hasPrevious = false;
        } else if (sourceTick != trackTick) {
            track.previous = track.current;
            track.current = source;
            track.hasCurrent = true;
            track.hasPrevious = PresentEntityContinuityCompatible(track.previous, track.current);
        } else {
            track.current = source;
        }
    }

    for (auto it = rawEntityTracks.begin(); it != rawEntityTracks.end();) {
        if (seenKeys.find(it->first) == seenKeys.end())
            it = rawEntityTracks.erase(it);
        else
            ++it;
    }
}

inline PresentEntityBuildResult BuildPresentEntitySet(
    const std::vector<OW::c_entity>& rawEntities,
    const std::unordered_map<uint64_t, RawPresentEntityTrack>& rawEntityTracks,
    uint32_t now,
    uint64_t nowUs,
    uint32_t delayMs) {
    PresentEntityBuildResult result{};
    result.entities.reserve(rawEntities.size());

    for (const OW::c_entity& source : rawEntities) {
        const uint64_t key = PresentEntityTrackKey(source);
        const OW::c_entity* previousRaw = nullptr;
        const OW::c_entity* currentRaw = &source;

        if (key != 0) {
            const auto found = rawEntityTracks.find(key);
            if (found != rawEntityTracks.end() && found->second.hasCurrent) {
                currentRaw = &found->second.current;
                if (found->second.hasPrevious)
                    previousRaw = &found->second.previous;
            }
        }

        OW::c_entity entity = BuildPresentEntity(*currentRaw, previousRaw, now, nowUs, delayMs);
        AddPresentSourceCount(result.counts, entity.present_source);
        if (entity.present_prediction_ms > 0.0f) {
            result.predictionSumMs += entity.present_prediction_ms;
            result.predictionMaxMs =
                (std::max)(result.predictionMaxMs, static_cast<double>(entity.present_prediction_ms));
            ++result.predictionSamples;
        }
        result.entities.push_back(std::move(entity));
    }

    return result;
}

inline void PublishPresentEntities(const std::vector<OW::c_entity>& rawEntities,
                                   const std::vector<OW::hpanddy>& rawDynamicEntities,
                                   std::unordered_map<uint64_t, RawPresentEntityTrack>& rawEntityTracks,
                                   uint32_t now,
                                   uint64_t nowUs,
                                   uint64_t expectedScene) {
    if(expectedScene!=OW::EntitySceneGeneration()) {rawEntityTracks.clear();return;}
    if (rawEntities.empty()) {
        rawEntityTracks.clear();
        {
            std::lock_guard<std::mutex> lock(OW::g_presentEntityMutex);
            if(expectedScene!=OW::EntitySceneGeneration())return;
            OW::present_entities.clear();
            OW::present_hp_dy_entities.clear();
        }
        {
            std::lock_guard<std::mutex> lock(OW::g_presentRenderEntityMutex);
            if(expectedScene!=OW::EntitySceneGeneration())return;
            OW::present_render_entities.clear();
            OW::present_render_hp_dy_entities.clear();
        }
        return;
    }

    UpdatePresentEntityTracks(rawEntities, rawEntityTracks);

    const uint32_t entityDelayMs = OW::PresentEntityDelayMs();
    const uint32_t renderEntityDelayMs = OW::PresentRenderEntityDelayMs();
    PresentEntityBuildResult presentSet =
        BuildPresentEntitySet(rawEntities, rawEntityTracks, now, nowUs, entityDelayMs);
    PresentEntityBuildResult renderPresentSet =
        renderEntityDelayMs == entityDelayMs
            ? presentSet
            : BuildPresentEntitySet(rawEntities, rawEntityTracks, now, nowUs, renderEntityDelayMs);

    {
        std::lock_guard<std::mutex> lock(OW::g_presentEntityMutex);
        if(expectedScene!=OW::EntitySceneGeneration())return;
        OW::present_entities = std::move(presentSet.entities);
        OW::present_hp_dy_entities = rawDynamicEntities;
    }
    {
        std::lock_guard<std::mutex> lock(OW::g_presentRenderEntityMutex);
        if(expectedScene!=OW::EntitySceneGeneration())return;
        OW::present_render_entities = std::move(renderPresentSet.entities);
        OW::present_render_hp_dy_entities = rawDynamicEntities;
    }

    Diagnostics::RecordEntityPresent(
        rawEntities.size(),
        presentSet.counts,
        presentSet.predictionSumMs,
        presentSet.predictionMaxMs,
        presentSet.predictionSamples);
    Diagnostics::RecordEntityRenderPresent(
        rawEntities.size(),
        renderPresentSet.counts,
        renderPresentSet.predictionSumMs,
        renderPresentSet.predictionMaxMs,
        renderPresentSet.predictionSamples);
}

void present_interp_thread() {
    if (!OW::PresentInterpolationEnabled()) {
        Diagnostics::Info("[PRESENT] entity render interpolation disabled.");
        return;
    }

    Diagnostics::Info(
        "[PRESENT] entity render interpolation enabled entity_hz=%lu max_extrap_ms=%lu entity_delay_ms=%lu render_entity_delay_ms=%lu render_micro_extrap_ms=%lu use_for_render=%d.",
        static_cast<unsigned long>(OW::PresentEntityHz()),
        static_cast<unsigned long>(OW::PresentMaxExtrapolationMs()),
        static_cast<unsigned long>(OW::PresentEntityDelayMs()),
        static_cast<unsigned long>(OW::PresentRenderEntityDelayMs()),
        static_cast<unsigned long>(OW::PresentRenderMicroExtrapolationMs()),
        OW::PresentUseForRenderEnabled() ? 1 : 0);

    std::unordered_map<uint64_t, RawPresentEntityTrack> rawEntityTracks;
    uint64_t presentScene=OW::EntitySceneGeneration();
    const auto entityPeriod = PresentPeriod(OW::PresentEntityHz());
    auto nextEntityPresent = std::chrono::steady_clock::now();

    while (OW::RuntimeWorkersRunning()) {
        const auto nowClock = std::chrono::steady_clock::now();
        if (nowClock >= nextEntityPresent) {
            const uint32_t now = GetTickCount();
            const uint64_t nowUs = PresentSteadyNowUs();
            const auto scene=OW::EntitySceneGeneration();
            if(scene!=presentScene){rawEntityTracks.clear();presentScene=scene;}
            const auto frame=OW::SnapshotFrame();
            const auto& rawEntities=frame->entities;
            const auto& rawDynamicEntities=frame->dynamic;
            PublishPresentEntities(rawEntities, rawDynamicEntities, rawEntityTracks, now, nowUs,scene);
            do {
                nextEntityPresent += entityPeriod;
            } while (nextEntityPresent <= nowClock);
        } else {
            OW::RuntimeWorkerSleep(0);
        }
    }
}
