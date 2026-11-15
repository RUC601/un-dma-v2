// Included after HeroSkillAutomation.inl, under the same runtime mutex.
HeroSkillSignals::SongSwitch g_songSwitch;
ProjectileThreat::Tracker g_projectileThreats;
struct BeamOutputRuntime {
    std::string key;
    uint64_t generation = 0;
    int button = -1;
};
BeamOutputRuntime g_beamOutput;
std::atomic<bool> g_beamOwnsOutputs{false};
std::string g_matrixAimKey;
bool SignalAimHeld() { return !g_matrixAimKey.empty() && TimedOutputScheduler().IsActive(g_matrixAimKey); }

void CancelBeamOutput() {
    if (!g_beamOutput.key.empty()) {
        TimedOutputScheduler().Cancel(g_beamOutput.key);
        ReleasePhysicalMouseMaskLease(g_beamOutput.key+":beam-mask",
            OutputOwnerSource::HeroTimedAction,g_beamOutput.generation);
    }
    g_beamOutput = {};
    g_beamOwnsOutputs.store(false,std::memory_order_release);
}

const c_entity* ConnectedBeamTarget(const c_entity& local,const std::vector<c_entity>& entities,uint32_t now) {
    const auto& signal=local.hero_skill_signals;
    if (!signal.mercyBeamKnown || !signal.mercyBeamConnected || !signal.mercyTargetId ||
        !SkillConditions::FreshSample(signal.sampledTick,now)) return nullptr;
    const c_entity* found=nullptr;
    for (const auto& entity : entities) {
        if (entity.match_id != signal.mercyTargetId) continue;
        // Ambiguity or a stale/reused handle never falls back to another ally.
        if (found || entity.address==local.address || !entity.team_known || entity.Team ||
            !entity.HasUsableBody(now,EntitySceneGeneration()) || !FreshHealthForSkill(entity)) return nullptr;
        found=&entity;
    }
    return found;
}

void EvaluateMercyBeam(const std::string& key,const Config::HeroSkillSettings& settings,const c_entity& local) {
    const auto input=KeyState::SampleVk(Labels::AimActivationKeyVk(settings.key));
    const auto entities=TargetingDetail::SnapshotEntities();
    const auto* target=ConnectedBeamTarget(local,entities,GetTickCount());
    OutputControl heal{},boost{};
    const int healVk=V2::HeroSettings::Key(local.HeroID,V2::HeroSettings::Action::Primary);
    const int boostVk=V2::HeroSettings::Key(local.HeroID,V2::HeroSettings::Action::Secondary);
    if (!input.available || !input.down || !target || !local.team_known || AnyInputSequenceActive() ||
        !TryMapVkToOutputControl(healVk,heal) || !TryMapVkToOutputControl(boostVk,boost) || heal==boost ||
        heal.kind!=OutputControlKind::MouseButton || boost.kind!=OutputControlKind::MouseButton ||
        Labels::AimActivationKeyVk(settings.key)==healVk || Labels::AimActivationKeyVk(settings.key)==boostVk) {
        CancelBeamOutput(); return;
    }
    const auto selected=SkillConditions::HealthAtOrBelow(target->PlayerHealth,target->PlayerHealthMax,
        settings.allyHealthThreshold,false) ? heal : boost;
    const auto generation=CurrentOutputRuntimeState().backendGeneration;
    if (!g_beamOutput.key.empty() && (g_beamOutput.key!=key || g_beamOutput.generation!=generation))
        CancelBeamOutput();
    g_beamOwnsOutputs.store(true,std::memory_order_release);
    TimedOutputScheduler().CancelSource(OutputOwnerSource::GlobalAim);
    TimedOutputScheduler().CancelSource(OutputOwnerSource::Trigger);
    if (!AcquirePhysicalMouseMaskLease(key+":beam-mask",OutputOwnerSource::HeroTimedAction,
        (1u<<heal.code)|(1u<<boost.code),generation)) { CancelBeamOutput(); return; }
    // Record the lease before any following operation can fail.
    g_beamOutput.key=key; g_beamOutput.generation=generation;
    if (g_beamOutput.button!=int(selected.code)) {
        const auto other=selected==heal ? boost : heal;
        if (V2::Output::DispatchForceReleaseMouseButtonForGeneration(generation,other.code)!=success) {
            CancelBeamOutput(); return;
        }
    }
    if (!TimedOutputScheduler().SetManualControlsForGeneration(key,OutputOwnerSource::HeroTimedAction,
        {selected},generation)) { CancelBeamOutput(); return; }
    g_beamOutput.button=selected.code;
}

bool EvaluateLucioSong(const std::string& key,const HeroSkillDefinition& definition,
                       const Config::HeroSkillSettings& settings,const c_entity& local) {
    const auto now=GetTickCount();
    if (!local.team_known || !SkillConditions::FreshSample(local.hero_skill_signals.sampledTick,now) ||
        local.hero_skill_signals.lucioSong<0 || !std::isfinite(settings.radius) || settings.radius<=0) {
        g_songSwitch.desired=-1; g_songSwitch.conditionSince=0; return false;
    }
    const auto source=CandidatePositionForAction(local);
    if (!IsUsablePosition(source)) { g_songSwitch.desired=-1; g_songSwitch.conditionSince=0; return false; }
    int wounded=0;
    for (const auto& ally : TargetingDetail::SnapshotEntities()) {
        if (ally.address==local.address || !ally.Alive) continue;
        if (!ally.team_known) { g_songSwitch.desired=-1; g_songSwitch.conditionSince=0; return false; }
        if (ally.Team) continue;
        if (!ally.HasUsableBody(now,EntitySceneGeneration()) || !FreshHealthForSkill(ally)) {
            // Missing teammate data is not evidence that nobody needs healing.
            g_songSwitch.desired=-1; g_songSwitch.conditionSince=0; return false;
        }
        if (settings.requireVisible && !ally.Vis) continue;
        const auto point=CandidatePositionForAction(ally);
        if (!IsUsablePosition(point)) { g_songSwitch.desired=-1; g_songSwitch.conditionSince=0; return false; }
        if (source.DistTo(point)<=settings.radius && SkillConditions::HealthAtOrBelow(
            ally.PlayerHealth,ally.PlayerHealthMax,settings.allyHealthThreshold,false)) ++wounded;
    }
    const int desired=wounded>=settings.minTargets ? 1 : 0;
    if (!g_songSwitch.Ready(local.hero_skill_signals.lucioSong,desired,
        local.hero_skill_signals.sampledTick,now,800) || IsActionDebounced(key)) return false;
    if (!StartSkillOutput(key,definition,settings,local,kBriefPulse)) return false;
    g_songSwitch.Submitted(local.hero_skill_signals.lucioSong,now);
    MarkActionExecuted(key);
    return true;
}

ProjectileThreat::Vec3 ThreatVector(const Vector3& value) { return {value.X,value.Y,value.Z}; }
void ObserveProjectileThreats(const c_entity& local) {
    const auto now=GetTickCount();
    if (!HeroSkillNeedsProjectileObservations(local.HeroID)) { g_projectileThreats.Reset(); return; }
    for (const auto& object : TargetingDetail::SnapshotDynamicEntities()) {
        if (object.scene_generation!=local.scene_generation) continue;
        g_projectileThreats.Observe({object.object_parent,object.entityid,object.scene_generation,object.MeshBase,
            object.position_tick_ms,object.status_tick_ms,object.team_key,object.team_known,
            {object.POS.x,object.POS.y,object.POS.z}},now);
    }
    g_projectileThreats.Prune(now);
}

bool EvaluateProjectileDefense(const std::string& key,const HeroSkillDefinition& definition,
                               const Config::HeroSkillSettings& settings,const c_entity& local) {
    if (definition.heroId==eHero::HERO_DVA && (!local.hero_skill_signals.dvaMechKnown ||
        !local.hero_skill_signals.dvaInMech || !SkillConditions::FreshSample(
            local.hero_skill_signals.dvaMechTick,GetTickCount()))) return false;
    if (!FreshLocalActionReady(definition,local) || IsActionDebounced(key)) return false;
    const bool health=settings.healthTriggerEnabled && FreshHealthForSkill(local) &&
        SkillConditions::HealthAtOrBelow(local.PlayerHealth,local.PlayerHealthMax,settings.healthThreshold,
            SkillControls(definition,HeroSkillControls::HealthAbsolute));
    uint64_t threatParent=0;
    ProjectileThreat::Approach nearest{};
    Vector3 view{};
    if (settings.projectileThreatEnabled && settings.projectileThreatMask && local.team_known &&
        TryReadCurrentViewAngles(view)) {
        const Vector3 source=CandidatePositionForAction(local);
        const Vector3 forward(std::cos(view.X)*std::sin(view.Y),-std::sin(view.X),std::cos(view.X)*std::cos(view.Y));
        const bool turn=definition.heroId==eHero::HERO_DVA && settings.automaticAim;
        for (const auto& object : TargetingDetail::SnapshotDynamicEntities()) {
            const int bit=object.entityid==ProjectileThreat::kBioticGrenade ? 1 :
                object.entityid==ProjectileThreat::kTacticalGrenade ? 2 : 0;
            const auto* track=g_projectileThreats.Find(object.object_parent);
            if (!(bit&settings.projectileThreatMask) || !track || object.scene_generation!=local.scene_generation) continue;
            auto next=ProjectileThreat::Evaluate(*track,ThreatVector(source),ThreatVector(local.velocity),
                ThreatVector(forward),local.team_key,GetTickCount(),settings.distance,250,1.5f,turn ? -1.f : .7071f);
            if (next.valid && (!nearest.valid || next.seconds<nearest.seconds)) { nearest=next;threatParent=object.object_parent; }
        }
        if (!health && nearest.valid && turn && nearest.facingCos<.7071f) {
            if (AnyInputSequenceActive() || g_automaticOwnsAim.load(std::memory_order_acquire) ||
                std::any_of(g_skillAimSessions.begin(),g_skillAimSessions.end(),[](const auto& item){return item.second.active;})) return false;
            SkillAimCandidate candidate{}; candidate.valid=true;
            const auto* track=g_projectileThreats.Find(threatParent);
            if (!track) return false;
            candidate.motionIdentity={threatParent,0,track->sample.component,local.connection_epoch,
                local.scene_generation,track->sample.type};
            candidate.motionSampleTick=track->sample.positionTick;
            candidate.rawAimPoint=candidate.aimPoint=Vector3(nearest.position.x,nearest.position.y,nearest.position.z);
            candidate.distance=source.DistTo(candidate.aimPoint); candidate.hitWindow=.5f;
            ScopedTrackingConfig tracking(settings.tracking);
            g_automaticOwnsAim.store(true,std::memory_order_release);
            TimedOutputScheduler().CancelSource(OutputOwnerSource::GlobalAim);
            TimedOutputScheduler().CancelSource(OutputOwnerSource::Trigger);
            MoveSkillAimAndCheckReady(key,settings,candidate,SkillProjectileRuntime{});
            return false; // Next observation must confirm the defensive direction.
        }
    }
    if (!health && !nearest.valid) return false;
    const auto hold=definition.heroId==eHero::HERO_DVA
        ? std::chrono::milliseconds(nearest.valid ? int((std::clamp)(nearest.seconds*1000.f+80.f,100.f,400.f)) : 250)
        : kBriefPulse;
    if (!StartSkillOutput(key,definition,settings,local,hold)) return false;
    if (definition.heroId==eHero::HERO_DVA && settings.automaticAim) {
        g_matrixAimKey=key;
        g_automaticOwnsAim.store(true,std::memory_order_release);
        TimedOutputScheduler().CancelSource(OutputOwnerSource::GlobalAim);
        TimedOutputScheduler().CancelSource(OutputOwnerSource::Trigger);
    }
    MarkActionExecuted(key);
    if (threatParent && nearest.facingCos>=.7071f) g_projectileThreats.MarkResponded(threatParent);
    return true;
}

void CancelSignalSkill(const std::string& key) {
    if (g_beamOutput.key==key) CancelBeamOutput();
    if (g_matrixAimKey==key) g_matrixAimKey.clear();
    if (key==std::to_string(eHero::HERO_LUCIO)+":crossfade") g_songSwitch.Reset();
}
void CancelSignalActions() {
    CancelBeamOutput(); g_songSwitch.Reset(); g_projectileThreats.Reset(); g_matrixAimKey.clear();
}
