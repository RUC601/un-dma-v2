// Included inside the skill runtime's anonymous namespace. All mutable state
// shares g_heroSkillRuntimeMutex and the existing output-generation boundary.
SkillConditions::RecastWindow g_returnWindow;
std::atomic<bool> g_specialOwnsAim{false};
std::atomic<bool> g_automaticOwnsAim{false};
std::unordered_map<std::string,SkillConditions::AutomaticAimWindow> g_automaticAimWindows;
struct GenjiBladeRuntime {
    GenjiAssist::BladeWindow window;
    SkillConditions::AutomaticAimWindow target;
    std::string key;
    uint64_t lastSwing = 0;
};
GenjiBladeRuntime g_genjiBlade;
std::atomic<bool> g_genjiBladeOwnsAim{false};

void CancelGenjiBlade() {
    if (!g_genjiBlade.key.empty()) {
        TimedOutputScheduler().Cancel(g_genjiBlade.key + ":dash");
        TimedOutputScheduler().Cancel(g_genjiBlade.key + ":swing");
        g_skillAimMotions.erase(g_genjiBlade.key);
    }
    g_genjiBlade = {};
    g_genjiBladeOwnsAim.store(false,std::memory_order_release);
}

bool IsSkillAimConfirmed(const SkillAimCandidate& candidate);

bool EvaluateSurvivalAction(const std::string& key, const HeroSkillDefinition& definition,
                            const Config::HeroSkillSettings& settings, const c_entity& local) {
    const auto now = GetTickCount();
    if (!local.health_sample_tick_ms || uint32_t(now-local.health_sample_tick_ms)>250) return false;
    if (definition.inputAction == HeroSkillInputAction::Ultimate) {
        if (!local.display_ultimate_tick || uint32_t(now-local.display_ultimate_tick)>250 ||
            !std::isfinite(local.ultimate) || local.ultimate<100) return false;
    } else {
        const size_t slot = definition.inputAction == HeroSkillInputAction::Ability1 ? 0 : 1;
        if (!local.skill_active_tick_ms[slot] || uint32_t(now-local.skill_active_tick_ms[slot])>250 ||
            (slot == 0 ? local.skill1act : local.skill2act)) return false;
    }
    return EvaluateHealthThresholdAction(key,definition,settings,local);
}

bool EvaluateRecentDamageDeflect(const std::string& key, const HeroSkillDefinition& definition,
                                const Config::HeroSkillSettings& settings, const c_entity& local) {
    const auto now = GetTickCount();
    if (!g_recentDamage.Ready(now) || !settings.healthTriggerEnabled ||
        !SkillConditions::HealthAtOrBelow(local.PlayerHealth,local.PlayerHealthMax,settings.healthThreshold,false) ||
        !local.skill_active_tick_ms[1] || uint32_t(now-local.skill_active_tick_ms[1])>250 || local.skill2act ||
        IsActionDebounced(key) || !std::isfinite(settings.distance) || settings.distance<=0) return false;
    const auto source = SourcePositionForAction(local);
    if (!IsUsablePosition(source)) return false;
    for (const auto& target : TargetingDetail::SnapshotEntities()) {
        if (!TargetingDetail::IsSelectableCandidate(target,0,local) || !target.Vis) continue;
        const auto point = CandidatePositionForAction(target);
        if (!IsUsablePosition(point)) continue;
        const float distance = source.DistTo(point);
        if (!std::isfinite(distance) || distance>settings.distance) continue;
        if (!StartTimedHotkey(key,SkillOutputVk(settings),kBriefPulse)) return false;
        MarkActionExecuted(key);
        Diagnostics::Info("Hero skill recent-damage deflect fired. skill=%s hp=%.1f damage=%.1f distance=%.2f",
            definition.skillId,local.PlayerHealth,g_recentDamage.damage,distance);
        g_recentDamage.ClearDamage();
        return true;
    }
    return false;
}

void EvaluateGenjiBlade(const std::string& key, const Config::HeroSkillSettings& settings,
                       const c_entity& local) {
    const auto input = KeyState::SampleVk(V2::HeroSettings::Key(local.HeroID,V2::HeroSettings::Action::Ultimate));
    const auto now = GetTickCount64();
    const bool wasActive = g_genjiBlade.window.active;
    const bool ready = g_genjiBlade.window.Advance(input.down,input.available,local.ultimate,now);
    if (!g_genjiBlade.window.active) {
        if (wasActive || !input.available) CancelGenjiBlade();
        return;
    }
    g_genjiBlade.key = key;
    g_genjiBladeOwnsAim.store(true,std::memory_order_release);
    if (!wasActive) {
        ResetAimSmoothingState();
        Diagnostics::Info("Genji blade assist started from manual ultimate input.");
        TimedOutputScheduler().CancelSource(OutputOwnerSource::GlobalAim);
        TimedOutputScheduler().CancelSource(OutputOwnerSource::Trigger);
    }
    if (!ready) return;
    ScopedTrackingConfig tracking(settings.tracking);
    SkillProjectileRuntime projectile{};
    projectile.maxRange = (std::min)(20.f,settings.distance);
    auto candidate = FindBestSkillAimCandidate(settings,projectile);
    if (!candidate.valid) { g_genjiBlade.target.Reset(); return; }
    if (!g_genjiBlade.target.Ready(SkillBodyIdentity(candidate.entity),now,settings.assistanceDelayMs,0)) return;
    // Rigel turns toward a dash target or a nearby swing target. UN verifies
    // the observed camera before sending either mapped action.
    candidate.hitWindow = (std::min)(candidate.hitWindow,0.8f);
    MoveSkillAimAndCheckReady(key,settings,candidate,projectile);
    if (!IsSkillAimConfirmed(candidate)) return;
    if (candidate.distance > GenjiAssist::kSwingRange) {
        TryStartGenjiDash(key + ":dash",V2::HeroSettings::Key(local.HeroID,V2::HeroSettings::Action::Ability1),local);
    } else if (!g_genjiBlade.lastSwing || now - g_genjiBlade.lastSwing >= GenjiAssist::kSwingMs) {
        if (StartTimedHotkey(key + ":swing",V2::HeroSettings::Key(local.HeroID,V2::HeroSettings::Action::Primary),kBriefPulse))
            g_genjiBlade.lastSwing = now;
    }
}
struct SuperJumpRuntime {
    SkillConditions::AimSession request;
    int phase = 0;
    uint64_t started = 0;
    uint64_t lastTick = 0;
    Vector3 initialAngles{};
};
std::unordered_map<std::string, SuperJumpRuntime> g_superJumps;

bool IsNormalInterruptCandidate(const std::string& key, int mask, const c_entity& entity,
    const SkillProjectileRuntime& projectile) {
    const int index = SkillConditions::NormalThreatIndex(entity.HeroID);
    if (index < 0 || !(mask & (1 << index))) return false;
    const auto& threat=SkillConditions::kNormalThreats[index];
    const auto slot = threat.slot;
    const auto state = g_defensiveThreats.find(entity.address);
    const auto source=SourcePositionForAction(TargetingDetail::SnapshotLocalEntity());
    const float travel=projectile.preFireDelayMs+(projectile.speed>0 ?
        source.DistTo(CandidatePositionForAction(entity))/projectile.speed*1000.f : 0.f);
    return state != g_defensiveThreats.end() && state->second.body == SkillBodyIdentity(entity) &&
        state->second.events[slot].CanRespond(4u) &&
        state->second.started[slot] && SkillConditions::InterruptCanArrive(threat,
            uint32_t(GetTickCount()-state->second.started[slot]),travel) &&
        !state->second.responders[slot].contains(key) &&
        SkillConditions::FreshActive(slot == 0 ? entity.skill1act : entity.skill2act,
            entity.skill_active_tick_ms[slot], GetTickCount());
}

bool IsSkillAimConfirmed(const SkillAimCandidate& candidate) {
    Vector3 current{};
    const auto source=SourcePositionForAction(TargetingDetail::SnapshotLocalEntity());
    return candidate.valid && candidate.hitWindow > 0 && TryReadCurrentViewAngles(current) &&
        OW::in_range(current, DirectionToAimEuler(candidate.aimPoint-source),source,candidate.aimPoint,candidate.hitWindow);
}

bool EvaluateAutomaticAim(const std::string& key, const HeroSkillDefinition& definition,
                          const Config::HeroSkillSettings& settings) {
    const auto kind = definition.automation;
    auto& window=g_automaticAimWindows[key];
    if (kind == SkillAutomation::ShieldBash) {
        const auto input = KeyState::SampleVk(V2::HeroSettings::Key(definition.heroId,V2::HeroSettings::Action::Secondary));
        if (!input.available || !input.down) { window.Reset(); return false; }
    }
    // A manual skill request or a running combo keeps its existing target.
    if (g_automaticOwnsAim.load(std::memory_order_acquire) || AnyInputSequenceActive() || g_rocketPunchOwnsAim.load(std::memory_order_acquire) ||
        std::any_of(g_skillAimSessions.begin(),g_skillAimSessions.end(),[](const auto& item){return item.second.active;})) {
        window.Reset();return false;
    }
    auto projectile = ResolveSkillProjectileRuntime(settings, GetTrajectoryParams(definition));
    ScopedTrackingConfig tracking(settings.tracking);
    const bool follows = kind == SkillAutomation::FollowAim;
    const auto selectedAddress = follows ? [&]() -> uint64_t {
        if (!Config::enableAimbot || !AimbotDetail::IsInputVkDown(
                Labels::AimActivationKeyVk(Config::View_aim_key()))) return 0;
        const auto targets = TargetingDetail::SnapshotEntities();
        const int index = Config::Targetenemyi;
        if (!TargetingDetail::IsValidIndex(index, targets.size())) return 0;
        return targets[index].address;
    }() : uint64_t(0);
    if (follows && !selectedAddress) { window.Reset();return false; }
    const bool canAim = settings.automaticAim &&
        (kind == SkillAutomation::LowEnemyHealth || kind == SkillAutomation::NormalInterrupt);
    const auto candidate = FindBestSkillAimCandidate(settings, projectile, 0,
        SkillControls(definition, HeroSkillControls::EnemyHealthAbsolute), [&](const c_entity& entity) {
            if (follows && entity.address != selectedAddress) return false;
            if (kind == SkillAutomation::NormalInterrupt)
                return IsNormalInterruptCandidate(key, settings.normalThreatMask, entity,projectile);
            return true;
        },!canAim);
    if (!candidate.valid) { window.Reset();return false; }
    if (!window.Ready(SkillBodyIdentity(candidate.entity),GetTickCount64(),settings.assistanceDelayMs,settings.maxAimTimeMs))
        return false;
    if (canAim) {
        g_automaticOwnsAim.store(true,std::memory_order_release);
        TimedOutputScheduler().CancelSource(OutputOwnerSource::GlobalAim);
        TimedOutputScheduler().CancelSource(OutputOwnerSource::Trigger);
        if (!MoveSkillAimAndCheckReady(key, settings, candidate, projectile)) return false;
    }
    // Check observed view, not a command which is still queued on the device.
    if (!IsSkillAimConfirmed(candidate) || IsActionDebounced(key)) return false;
    const auto local = TargetingDetail::SnapshotLocalEntity();
    const bool genjiDash = definition.heroId == eHero::HERO_GENJI && definition.inputAction == HeroSkillInputAction::Ability1;
    if (!(genjiDash ? TryStartGenjiDash(key,SkillOutputVk(settings),local)
                   : StartTimedHotkey(key,SkillOutputVk(settings),kBriefPulse))) return false;
    RecordTrajectoryCast(key,definition,settings);
    window.Reset();
    if (kind == SkillAutomation::NormalInterrupt) {
        const int index = SkillConditions::NormalThreatIndex(candidate.entity.HeroID);
        g_defensiveThreats[candidate.entity.address].responders[SkillConditions::kNormalThreats[index].slot].insert(key);
    }
    Diagnostics::Info("Hero automatic skill fired. skill=%s target=0x%llX mode=%d", definition.skillId,
        static_cast<unsigned long long>(candidate.entity.address), static_cast<int>(kind));
    return true;
}

bool EvaluateReturn(const std::string& key, const Config::HeroSkillSettings& settings, const c_entity& local) {
    if (!SkillConditions::FreshActive(local.skill1act,local.skill_active_tick_ms[0],GetTickCount()) ||
        !g_returnWindow.Ready(GetTickCount64()) ||
        !SkillConditions::HealthAtOrBelow(local.PlayerHealth,local.PlayerHealthMax,settings.healthThreshold,true)) return false;
    if (!StartTimedHotkey(key,settings.runtimeOutputVk,kBriefPulse)) return false;
    g_returnWindow.spent = true;
    MarkActionExecuted(key);
    return true;
}

void CancelSuperJump(const std::string& key, SuperJumpRuntime& state) {
    TimedOutputScheduler().Cancel(key);
    state.phase = 0;
    state.request.Block();
    g_specialOwnsAim.store(false,std::memory_order_release);
}

std::optional<OutputActionPlan> BuildEmreJumpPlan(const std::string& key,
    const Config::HeroSkillSettings& settings,uint64_t hero) {
    const auto action = [&](V2::HeroSettings::Action a, OutputControl& out) {
        return TryMapVkToOutputControl(V2::HeroSettings::Key(hero,a),out);
    };
    OutputControl grenade{},siphon{},forward{},jump{};
    if (!action(V2::HeroSettings::Action::Ability2,grenade) ||
        !action(V2::HeroSettings::Action::Ability1,siphon) ||
        !action(V2::HeroSettings::Action::Forward,forward) ||
        !action(V2::HeroSettings::Action::Jump,jump) ||
        grenade==siphon || grenade==forward || grenade==jump || siphon==forward ||
        siphon==jump || forward==jump) return std::nullopt;
    OutputControl activation{};
    if(TryMapVkToOutputControl(Labels::AimActivationKeyVk(settings.key),activation) &&
        (activation==grenade || activation==siphon || activation==forward || activation==jump)) return std::nullopt;
    const int moveStart=20+settings.jumpLeadMs;
    const int jumpAt=moveStart+settings.moveForwardMs;
    OutputActionPlan plan;
    plan.key=key;plan.ownerSource=OutputOwnerSource::HeroTimedAction;plan.acquire={grenade};
    plan.steps={
        {std::chrono::milliseconds(20),{},{grenade},{},false},
        {std::chrono::milliseconds(moveStart),{siphon,forward},{},{},false},
        {std::chrono::milliseconds(moveStart+20),{},{siphon},{},false},
        {std::chrono::milliseconds(jumpAt),{jump},{forward},{},false},
        {std::chrono::milliseconds(jumpAt+40),{},{jump},{},true}};
    return plan;
}

void EvaluateSuperJump(const std::string& key, const HeroSkillDefinition& definition,
                       const Config::HeroSkillSettings& settings, const c_entity& local) {
    auto& state = g_superJumps[key];
    const auto input = KeyState::SampleVk(Labels::AimActivationKeyVk(settings.key));
    const auto now = GetTickCount64();
    if (!state.phase) {
        if (!state.request.Advance(input.down,now,0,0,input.available)) return;
        state.request.Block();
        if (!BuildEmreJumpPlan(key,settings,local.HeroID)) return;
        if (!TryReadCurrentViewAngles(state.initialAngles)) return;
        if (IsManualCooldownActive(key,settings)) return;
        auto shiftDefinition=definition;
        shiftDefinition.inputAction=HeroSkillInputAction::Ability1;
        if (IsInRechargeInterval(definition,settings,local) || IsInRechargeInterval(shiftDefinition,settings,local)) return;
        state.phase = 1;state.started = now;state.lastTick=now;
        g_specialOwnsAim.store(true,std::memory_order_release);
        TimedOutputScheduler().CancelSource(OutputOwnerSource::GlobalAim);
        TimedOutputScheduler().CancelSource(OutputOwnerSource::Trigger);
    }
    if (now - state.started > 2500 || !input.available) { CancelSuperJump(key,state); return; }
    const float dt=std::clamp(float(now-state.lastTick)/1000.f,0.001f,0.05f);
    state.lastTick=now;
    if (state.phase == 1) {
        const float down = ClampViewPitchTarget(settings.pitchDownTargetAngle*kDegToRad);
        if (!IsPitchWithin(down,2.f*kDegToRad)) {
            MovePitchToward(down,2000.f,dt);return;
        }
        auto plan=BuildEmreJumpPlan(key,settings,local.HeroID);
        if (!plan || !TimedOutputScheduler().Schedule(std::move(*plan))) {CancelSuperJump(key,state);return;}
        state.phase=2;MarkActionExecuted(key);
        g_lastOutputActions[V2::HeroSettings::Key(local.HeroID,V2::HeroSettings::Action::Ability1)]=Clock::now();
        g_lastOutputActions[V2::HeroSettings::Key(local.HeroID,V2::HeroSettings::Action::Ability2)]=Clock::now();
    }
    if (state.phase==2) {
        MovePitchToward(state.initialAngles.X,2000.f,dt);
        if (!TimedOutputScheduler().IsActive(key) && IsPitchWithin(state.initialAngles.X,2.f*kDegToRad)) {
            state.phase=0;g_specialOwnsAim.store(false,std::memory_order_release);
        }
    }
}

void CancelAutomation(const std::string& key) {
    const auto found=g_superJumps.find(key);
    if(found!=g_superJumps.end())CancelSuperJump(key,found->second);
    g_automaticAimWindows.erase(key);
}
