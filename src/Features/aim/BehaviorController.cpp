#ifdef UNLEASHED_SELF_TESTS
#include "output/MockBackend.hpp"
#endif
#include "Game/WeaponCharge.hpp"
#include <optional>
#include "BehaviorController.hpp"
#include "Game/Overwatch.hpp"

// =========================================================================
// Main aimbot thread
// =========================================================================

// ---- Aim diagnostic counters ----
static uint64_t g_trackingAttempts = 0;
static uint64_t g_trackingMoves = 0;
static uint64_t g_flickAttempts = 0;
static uint64_t g_flickFires = 0;

namespace OW {

bool RefreshAutoGameMouseSensitivity(bool force) {
    static DWORD lastRefreshTick = 0;

    if (!OW::Config::autoReadGameMouseSensitivity) {
        OW::Config::detectedGameMouseSensitivity = 0.0f;
        OW::Config::gameMouseSensitivityAutoDetected = false;
        return false;
    }

    const DWORD now = GetTickCount();
    if (!force && lastRefreshTick != 0 && now - lastRefreshTick < 1000)
        return OW::Config::gameMouseSensitivityAutoDetected;
    lastRefreshTick = now;

    float sensitivity = 0.0f;
    uint64_t sourceObject = 0;
    if (!OW::TryReadGameMouseSensitivity(sensitivity, &sourceObject)) {
        if (OW::Config::gameMouseSensitivityAutoDetected) {
            Diagnostics::Aim("game_sens.auto_read lost previous=%.6f manual=%.6f",
                OW::Config::detectedGameMouseSensitivity,
                OW::Config::gameMouseSensitivity);
        }
        OW::Config::detectedGameMouseSensitivity = 0.0f;
        OW::Config::gameMouseSensitivityAutoDetected = false;
        return false;
    }

    const bool changed =
        !OW::Config::gameMouseSensitivityAutoDetected ||
        std::fabs(OW::Config::detectedGameMouseSensitivity - sensitivity) > 0.001f;
    OW::Config::detectedGameMouseSensitivity = sensitivity;
    OW::Config::gameMouseSensitivityAutoDetected = true;
    if (changed) {
        Diagnostics::Aim("game_sens.auto_read value=%.6f object=0x%llX source=globaladmin_singleton_0x6",
            sensitivity,
            static_cast<unsigned long long>(sourceObject));
    }
    return true;
}

} // namespace OW

namespace AimbotDetail {
    inline thread_local RuntimeState* activeAimShotState=nullptr;
    inline bool ResetAimCharge(RuntimeState& state,bool requireSubmission=false);
    inline void CancelConfiguredAimShot(RuntimeState& state);

    inline OW::c_entity LocalEntity();
    inline bool IsConfiguredActivationKeyPressedForSelection(int keySetting,
                                                             int* matchedKeySetting = nullptr);
    inline bool IsInputVkDownQuiet(int vk);

    inline int PhysicalMouseVkForAction(int action) {
        switch (OW::MouseButtonForAttackAction(action)) {
        case 0: return VK_LBUTTON;
        case 1: return VK_RBUTTON;
        case 2: return VK_MBUTTON;
        default: return 0;
        }
    }

    inline bool RequiredAimActionIsHeld(const OW::Config::HeroPreset& preset) {
        if (!preset.requireActionHeld)
            return true;
        return IsInputVkDownQuiet(PhysicalMouseVkForAction(preset.trigger.action));
    }

    struct TrackingSessionIdentity {
        uint64_t slotId = 0;
        uint64_t heroId = 0;
        int slotIndex = -1;
        int aimKey = 0;
    };

    struct RuntimePresetSelection {
        OW::Config::HeroPreset preset{};
        int slotIndex = -1;
        bool matchedInput = false;
        int activationKeyOverride = -1;
        bool usedSideButtonAlias = false;
        OW::Config::AimScopeRequirement scopeRequirement =
            OW::Config::AimScopeRequirement::All;
        bool rmbHeld = false;
    };
    // The selection contains vectors; keep it outside the SEH worker's POD state.
    inline thread_local std::optional<RuntimePresetSelection> chargedAimPreset;

    inline bool UsesReleaseKeyUpExecution() {
        const auto* tuning=OW::Config::ActiveSlotBehaviorTuning();
        return tuning && tuning->releaseToFire && tuning->autoCharge &&
            tuning->releaseExecution==1 && OW::Config::UsesFlickFireControls(OW::Config::View_aimBehavior());
    }

    inline TrackingSessionIdentity CurrentTrackingSessionIdentity() {
        TrackingSessionIdentity identity{};
        identity.heroId = LocalEntity().HeroID;
        identity.aimKey = OW::Config::View_aim_key();

        const OW::Config::RuntimeDrawFovState drawFov = OW::Config::SnapshotRuntimeDrawFov();
        if (drawFov.active &&
            drawFov.slotKind == static_cast<int>(OW::Config::FovRingSlotKind::Aim)) {
            identity.slotIndex = drawFov.slotIndex;
            OW::Config::HeroSlotPreset slot;
            if (OW::Config::TryGetHeroAimSlot(identity.heroId, identity.slotIndex, slot))
                identity.slotId = slot.stableId;
        }

        return identity;
    }

    inline bool SameTrackingSessionIdentity(const RuntimeState& state,
                                            const TrackingSessionIdentity& identity) {
        return state.trackingSessionSlotId == identity.slotId &&
            state.trackingSessionHeroId == identity.heroId &&
            state.trackingSessionSlotIndex == identity.slotIndex &&
            state.trackingSessionAimKey == identity.aimKey;
    }





    inline void PrepareMagneticTriggerQuantization(
        RuntimeState& state,
        std::uint64_t sessionGeneration,
        std::uint64_t connectionEpoch,
        std::uint64_t targetKey) {
        const OW::MouseMoveQuantizationState previous =
            state.magneticMoveQuantization;
        if (!OW::PrepareMouseMoveQuantizationState(
                state.magneticMoveQuantization,
                sessionGeneration,
                connectionEpoch,
                targetKey)) {
            return;
        }

        const char* reason = "state_start";
        if (previous.initialized) {
            if (previous.sessionGeneration != sessionGeneration)
                reason = "session_generation_changed";
            else if (previous.connectionEpoch != connectionEpoch)
                reason = "connection_generation_changed";
            else if (previous.targetKey != targetKey)
                reason = "target_changed";
        }
        if (OW::HasMouseMoveQuantizationState(previous) ||
            OW::Config::aimVerboseLog) {
            LogMagneticTriggerQuantizationReset(
                reason,
                OW::HasMouseMoveQuantizationState(previous));
        }
    }



    inline void EnsureTrackingSession(RuntimeState& state,
                                      const TrackingSessionIdentity& identity) {
        if (state.trackingSessionActive && SameTrackingSessionIdentity(state, identity))
            return;

        state.trackingSessionActive = true;
        ++state.trackingSessionGeneration;
        if (state.trackingSessionGeneration == 0)
            ++state.trackingSessionGeneration;
        state.trackingSessionHeroId = identity.heroId;
        state.trackingSessionSlotIndex = identity.slotIndex;
        state.trackingSessionSlotId = identity.slotId;
        state.trackingSessionAimKey = identity.aimKey;
        OW::ResetAimStartLimiter(state.trackingStartLimiter);
        OW::ResetAimSmoothingState();
        Diagnostics::Aim("tracking.session start hero=0x%llX slot=%d key=%d",
            static_cast<unsigned long long>(identity.heroId),
            identity.slotIndex + 1,
            identity.aimKey);
    }

    inline bool DistancePassesAimPresetRange(float distance, const OW::Config::HeroPreset& preset) {
        if (!std::isfinite(distance))
            return false;

        const float minDistance = std::clamp(preset.minDistance, 0.0f, 500.0f);
        const float maxDistance = std::clamp(preset.maxDistance, 0.0f, 500.0f);
        if (minDistance > 0.0f && distance < minDistance)
            return false;
        if (maxDistance > 0.0f && distance > maxDistance)
            return false;
        return true;
    }

    inline int ResolvePresetAimBoneForDistance(const OW::Config::HeroPreset& preset, float distance) {
        const int normalized = OW::Config::NormalizeAimBone(preset.bone);
        const bool headRequested = normalized == OW::Config::kAimBoneHead;
        const float headGate = std::clamp(preset.maxHeadDistance, 0.0f, 500.0f);
        if (headRequested && headGate > 0.0f && distance > headGate)
            return OW::Config::kAimBoneNeck;
        return normalized;
    }

    inline bool AimPresetHasSelectableEntityInDistanceRange(const OW::Config::HeroPreset& preset) {
        const std::vector<OW::c_entity> entities = OW::TargetingDetail::SnapshotEntities();
        if (entities.empty())
            return false;

        const OW::c_entity local = OW::TargetingDetail::SnapshotLocalEntity();
        const Vector3 camera = OW::TargetingDetail::CameraPosition();
        if (!OW::TargetingDetail::IsFiniteVector(camera) ||
            OW::TargetingDetail::IsZeroVector(camera))
            return false;

        const OW::TargetingDetail::FovRuntimeContext fovContext =
            OW::TargetingDetail::SnapshotFovRuntimeContext();
        if (!fovContext.valid)
            return false;

        for (OW::c_entity entity : entities) {
            const auto effective=OW::AimPlan::Resolve(preset.aimPlan,entity.HeroID);
            if(!OW::AimPlan::Eligible(preset.aimPlan.selection,effective,entity.HeroID,entity.PlayerHealth,entity.PlayerHealthMax,entity.Vis)) continue;
            auto inRange=[&](float d) { return effective.overrideLimits ? std::isfinite(d) && d>=effective.limits.minDistance &&
                (effective.limits.maxDistance<=0 || d<=effective.limits.maxDistance) : DistancePassesAimPresetRange(d,preset); };
            if (!OW::TargetingDetail::IsRuntimeTargetValid(entity, false))
                continue;
            if (!OW::TargetingDetail::TargetTeamMatches(entity, preset.targetTeam, local))
                continue;
            if (preset.ignoreInvisible && !entity.Vis)
                continue;

            const float chestDistance = camera.DistTo(entity.chest_pos);
            if (!inRange(chestDistance))
                continue;

            const int aimBone = effective.overrideAimPoint ? (effective.point.boneMask&1 ? OW::Config::kAimBoneHead :
                effective.point.boneMask&2 ? OW::Config::kAimBoneNeck : OW::Config::kAimBoneChest) : ResolvePresetAimBoneForDistance(preset, chestDistance);
            const Vector3 aimPoint = OW::TargetingDetail::ConfiguredBonePosition(entity, aimBone);
            if (!OW::TargetingDetail::IsFiniteVector(aimPoint) ||
                OW::TargetingDetail::IsZeroVector(aimPoint))
                continue;

            const float aimDistance = camera.DistTo(aimPoint);
            if (!inRange(aimDistance))
                continue;

            const float effectiveFovDeg =
                effective.overrideLimits?effective.limits.fov:OW::Config::ResolveRuntimeHeroPresetFovForDistance(preset, aimDistance);
            if (OW::TargetingDetail::IsWithinFovDeg(fovContext, aimPoint, effectiveFovDeg))
                return true;
        }

        return false;
    }

    inline bool AimPresetHasEntityInDistanceRange(const OW::Config::HeroPreset& preset) {
        const std::vector<OW::c_entity> entities = OW::TargetingDetail::SnapshotEntities();
        if (entities.empty())
            return false;

        const Vector3 camera = OW::TargetingDetail::CameraPosition();
        if (!OW::TargetingDetail::IsFiniteVector(camera) ||
            OW::TargetingDetail::IsZeroVector(camera))
            return false;

        for (const OW::c_entity& entity : entities) {
            const auto effective=OW::AimPlan::Resolve(preset.aimPlan,entity.HeroID);
            if(!OW::AimPlan::Eligible(preset.aimPlan.selection,effective,entity.HeroID,entity.PlayerHealth,entity.PlayerHealthMax,entity.Vis)) continue;
            if (!OW::TargetingDetail::IsRuntimeTargetValid(entity, false))
                continue;

            const float chestDistance = camera.DistTo(entity.chest_pos);
            if (effective.overrideLimits ? chestDistance>=effective.limits.minDistance && (effective.limits.maxDistance<=0 || chestDistance<=effective.limits.maxDistance) : DistancePassesAimPresetRange(chestDistance,preset))
                return true;
        }

        return false;
    }

    inline void LogRuntimePresetSelection(const char* kind,
                                          uint64_t heroId,
                                          const RuntimePresetSelection& selection) {
        struct LastState {
            bool initialized = false;
            uint64_t heroId = 0;
            int slotIndex = -2;
            bool matchedInput = false;
            int key = -1;
            int activationKeyOverride = -1;
            bool usedSideButtonAlias = false;
            int scopeRequirement = -1;
            bool rmbHeld = false;
            int weaponAction = -1;
            float fov = -1.0f;
            int aimBehavior = -1;
            int triggerMode = -1;
            int triggerKey = -1;
            bool triggerEnabled = false;
        };

        static LastState aimState{};
        static LastState triggerState{};
        const bool isTriggerKind = kind && std::strcmp(kind, "trigger") == 0;
        LastState& state = isTriggerKind ? triggerState : aimState;

        const OW::Config::HeroPreset& preset = selection.preset;
        const bool changed = !state.initialized ||
            state.heroId != heroId ||
            state.slotIndex != selection.slotIndex ||
            state.matchedInput != selection.matchedInput ||
            state.key != preset.key ||
            state.activationKeyOverride != selection.activationKeyOverride ||
            state.usedSideButtonAlias != selection.usedSideButtonAlias ||
            state.scopeRequirement != static_cast<int>(selection.scopeRequirement) ||
            state.rmbHeld != selection.rmbHeld ||
            state.weaponAction != preset.trigger.action ||
            (!isTriggerKind && std::fabs(state.fov - preset.fov) > 0.001f) ||
            state.aimBehavior != preset.aimBehavior ||
            state.triggerMode != preset.trigger.mode ||
            state.triggerKey != preset.trigger.key ||
            state.triggerEnabled != preset.trigger.enabled;

        if (!changed)
            return;

        const int behaviorMethod = OW::Config::AimBehaviorMethod(preset.aimBehavior);
        if (isTriggerKind) {
            Diagnostics::Aim("hero_preset.runtime kind=%s hero=0x%llX slot=%d matchedInput=%d key=%d activeKey=%d sideAlias=%d scopeRequirement=%s rmbHeld=%d weaponAction=%d aimMode=%d aimBehavior=%d behaviorMethod=%d triggerEnabled=%d triggerMode=%d triggerKey=%d",
                kind,
                static_cast<unsigned long long>(heroId),
                selection.slotIndex + 1,
                selection.matchedInput ? 1 : 0,
                preset.key,
                selection.activationKeyOverride,
                selection.usedSideButtonAlias ? 1 : 0,
                OW::Config::AimScopeRequirementName(selection.scopeRequirement),
                selection.rmbHeld ? 1 : 0,
                preset.trigger.action,
                preset.aimMode,
                preset.aimBehavior,
                behaviorMethod,
                preset.trigger.enabled ? 1 : 0,
                preset.trigger.mode,
                preset.trigger.key);
        } else {
            Diagnostics::Aim("hero_preset.runtime kind=%s hero=0x%llX slot=%d matchedInput=%d key=%d activeKey=%d sideAlias=%d scopeRequirement=%s rmbHeld=%d weaponAction=%d fovDeg=%.2f aimMode=%d aimBehavior=%d behaviorMethod=%d triggerEnabled=%d triggerMode=%d triggerKey=%d",
                kind ? kind : "unknown",
                static_cast<unsigned long long>(heroId),
                selection.slotIndex + 1,
                selection.matchedInput ? 1 : 0,
                preset.key,
                selection.activationKeyOverride,
                selection.usedSideButtonAlias ? 1 : 0,
                OW::Config::AimScopeRequirementName(selection.scopeRequirement),
                selection.rmbHeld ? 1 : 0,
                preset.trigger.action,
                preset.fov,
                preset.aimMode,
                preset.aimBehavior,
                behaviorMethod,
                preset.trigger.enabled ? 1 : 0,
                preset.trigger.mode,
                preset.trigger.key);
        }

        state.initialized = true;
        state.heroId = heroId;
        state.slotIndex = selection.slotIndex;
        state.matchedInput = selection.matchedInput;
        state.key = preset.key;
        state.activationKeyOverride = selection.activationKeyOverride;
        state.usedSideButtonAlias = selection.usedSideButtonAlias;
        state.scopeRequirement = static_cast<int>(selection.scopeRequirement);
        state.rmbHeld = selection.rmbHeld;
        state.weaponAction = preset.trigger.action;
        state.fov = preset.fov;
        state.aimBehavior = preset.aimBehavior;
        state.triggerMode = preset.trigger.mode;
        state.triggerKey = preset.trigger.key;
        state.triggerEnabled = preset.trigger.enabled;
    }

    inline bool TrySelectRuntimeAimPreset(uint64_t heroId, RuntimePresetSelection& selection) {
        OW::Config::ScopedConfigRead configRead;
        const bool rmbHeld = IsInputVkDownQuiet(VK_RBUTTON);
        struct IndexedAimSlot {
            OW::Config::HeroSlotPreset slot{};
            int slotIndex = -1;
        };
        std::array<IndexedAimSlot, OW::Config::kMaxHeroPresetSlots> enabledSlots{};
        int enabledSlotCount = 0;
        for (int slotIndex = 0; slotIndex < OW::Config::kMaxHeroPresetSlots; ++slotIndex) {
            OW::Config::HeroSlotPreset slot{};
            if (!OW::Config::TryGetHeroAimSlot(heroId, slotIndex, slot) || !slot.enabled)
                continue;
            if (!RequiredAimActionIsHeld(slot.preset))
                continue;
            enabledSlots[static_cast<size_t>(enabledSlotCount++)] = { slot, slotIndex };
        }

        RuntimePresetSelection fallback{};
        int fallbackPriority = -1;

        for (int index = 0; index < enabledSlotCount; ++index) {
            const IndexedAimSlot& candidate = enabledSlots[static_cast<size_t>(index)];
            const OW::Config::HeroSlotPreset& slot = candidate.slot;

            const int priority = OW::Config::AimScopeFallbackSelectionPriority(
                slot.scopeRequirement,
                rmbHeld);
            if (priority <= fallbackPriority)
                continue;

            fallback.preset = slot.preset;
            fallback.slotIndex = candidate.slotIndex;
            fallback.matchedInput = false;
            fallback.activationKeyOverride = slot.preset.key;
            fallback.scopeRequirement = slot.scopeRequirement;
            fallback.rmbHeld = rmbHeld;
            fallbackPriority = priority;
        }

        auto tryKeyedSelection = [&](int requiredPriority,
                                     RuntimePresetSelection& outSelection) {
            RuntimePresetSelection inputFallback{};
            RuntimePresetSelection distanceFallback{};
            bool hasInputFallback = false;
            bool hasDistanceFallback = false;

            for (int index = 0; index < enabledSlotCount; ++index) {
                const IndexedAimSlot& candidate = enabledSlots[static_cast<size_t>(index)];
                const OW::Config::HeroSlotPreset& slot = candidate.slot;

                if (OW::Config::AimScopePressedSelectionPriority(
                        slot.scopeRequirement,
                        rmbHeld,
                        slot.preset.key) != requiredPriority) {
                    continue;
                }

                int matchedKeySetting = slot.preset.key;
                const bool keyPressed = slot.preset.key == 0
                    ? rmbHeld
                    : IsConfiguredActivationKeyPressedForSelection(
                        slot.preset.key,
                        &matchedKeySetting);
                if (!keyPressed)
                    continue;

                const bool usedAlias = matchedKeySetting != slot.preset.key;

                RuntimePresetSelection keyedSelection{};
                keyedSelection.preset = slot.preset;
                keyedSelection.slotIndex = candidate.slotIndex;
                keyedSelection.matchedInput = true;
                keyedSelection.activationKeyOverride = matchedKeySetting;
                keyedSelection.usedSideButtonAlias = usedAlias;
                keyedSelection.scopeRequirement = slot.scopeRequirement;
                keyedSelection.rmbHeld = rmbHeld;

                if (!hasInputFallback) {
                    inputFallback = keyedSelection;
                    hasInputFallback = true;
                }

                if (AimPresetHasSelectableEntityInDistanceRange(slot.preset)) {
                    outSelection = keyedSelection;
                    return true;
                }

                if (!hasDistanceFallback && AimPresetHasEntityInDistanceRange(slot.preset)) {
                    distanceFallback = keyedSelection;
                    hasDistanceFallback = true;
                }
            }

            if (hasDistanceFallback) {
                outSelection = distanceFallback;
                return true;
            }

            if (hasInputFallback) {
                outSelection = inputFallback;
                return true;
            }

            return false;
        };

        // Scoped explicit hotkeys outrank the RMB tracking baseline, and both
        // scoped forms outrank All while RMB is held.
        for (int priority = 3; priority >= 1; --priority) {
            if (tryKeyedSelection(priority, selection))
                return true;
        }

        if (fallbackPriority < 0)
            return false;

        selection = fallback;
        return true;
    }

    inline bool TrySelectRuntimeTriggerPreset(uint64_t heroId, RuntimePresetSelection& selection) {
        OW::Config::ScopedConfigRead configRead;
        static uint64_t stickyHeroId = 0;
        static int stickyToggleSlotIndex = -1;
        static uint64_t stickyToggleSlotId = 0;
        if (stickyHeroId != heroId) {
            stickyHeroId = heroId;
            stickyToggleSlotIndex = -1;
        }

        RuntimePresetSelection fallback{};
        RuntimePresetSelection presentFallback{};
        RuntimePresetSelection alwaysCandidate{};
        bool hasFallback = false;
        bool hasPresentFallback = false;
        bool hasAlwaysCandidate = false;

        for (int slotIndex = 0; slotIndex < OW::Config::kMaxHeroPresetSlots; ++slotIndex) {
            OW::Config::HeroSlotPreset slot{};
            if (!OW::Config::TryGetHeroTriggerSlot(heroId, slotIndex, slot))
                continue;

            if (!hasPresentFallback) {
                presentFallback.preset = slot.preset;
                presentFallback.preset.trigger.enabled = false;
                presentFallback.slotIndex = slotIndex;
                presentFallback.matchedInput = false;
                hasPresentFallback = true;
            }

            if (!slot.enabled)
                continue;

            if (!hasFallback) {
                fallback.preset = slot.preset;
                fallback.slotIndex = slotIndex;
                fallback.matchedInput = false;
                hasFallback = true;
            }

            const OW::Config::TriggerPreset& trigger = slot.preset.trigger;
            if (!trigger.enabled)
                continue;

            if ((trigger.mode == 0 || trigger.mode == 1) &&
                IsConfiguredActivationKeyPressedForSelection(trigger.key)) {
                stickyToggleSlotIndex = trigger.mode == 1 ? slotIndex : -1;
                stickyToggleSlotId = trigger.mode == 1 ? slot.stableId : 0;
                selection.preset = slot.preset;
                selection.slotIndex = slotIndex;
                selection.matchedInput = true;
                return true;
            }

            if (trigger.mode == 2 && !hasAlwaysCandidate) {
                alwaysCandidate.preset = slot.preset;
                alwaysCandidate.slotIndex = slotIndex;
                alwaysCandidate.matchedInput = true;
                hasAlwaysCandidate = true;
            }
        }

        if (stickyToggleSlotIndex >= 0 && OW::Config::triggerbotToggleActive) {
            OW::Config::HeroSlotPreset stickySlot{};
            if (OW::Config::TryGetHeroTriggerSlot(heroId, stickyToggleSlotIndex, stickySlot) &&
                stickySlot.stableId == stickyToggleSlotId &&
                stickySlot.enabled &&
                stickySlot.preset.trigger.enabled &&
                stickySlot.preset.trigger.mode == 1) {
                selection.preset = stickySlot.preset;
                selection.slotIndex = stickyToggleSlotIndex;
                selection.matchedInput = true;
                return true;
            }
            stickyToggleSlotIndex = -1;
        }

        if (hasAlwaysCandidate) {
            stickyToggleSlotIndex = -1;
            selection = alwaysCandidate;
            return true;
        }

        if (!hasFallback) {
            if (!OW::ShouldApplyDisabledHeroTriggerFallback(
                    hasPresentFallback,
                    hasFallback))
                return false;
            selection = presentFallback;
            return true;
        }

        selection = fallback;
        return true;
    }

    inline bool SelectAimPresetForActivation(RuntimeState& state,uint64_t heroId,RuntimePresetSelection& selection) {
        if(chargedAimPreset) {
            OW::Config::HeroSlotPreset current;
            bool valid=heroId==state.releaseSession.heroId &&
                OW::Config::TryGetHeroAimSlot(heroId,chargedAimPreset->slotIndex,current) &&
                current.enabled && current.stableId==state.releaseSession.slotId;
            if(valid) {
                OW::Config::ResolveHeroBehaviorMode(current.preset);
                const auto& captured=chargedAimPreset->preset;
                valid=current.preset.aimBehavior==captured.aimBehavior &&
                    current.preset.aimMethod==captured.aimMethod &&
                    current.preset.aimBehaviorPresetId==captured.aimBehaviorPresetId &&
                    current.preset.key==captured.key && current.preset.trigger.action==captured.trigger.action &&
                    current.preset.releaseToFire && current.preset.autoCharge && current.preset.releaseExecution==1;
            }
            if(valid) selection=*chargedAimPreset;
            else {
                CancelConfiguredAimShot(state);
                if(chargedAimPreset) selection=*chargedAimPreset;
            }
        }
        if(!chargedAimPreset && (!heroId || !TrySelectRuntimeAimPreset(heroId, selection))) {
            return false;
        }
        if(!chargedAimPreset && selection.matchedInput && selection.preset.releaseToFire &&
            selection.preset.autoCharge && selection.preset.releaseExecution==1 &&
            OW::Config::UsesFlickFireControls(selection.preset.aimBehavior)) chargedAimPreset=selection;
        return true;
    }

    struct ScopedHeroPresetOverride {
        bool previousPlanEnabled=OW::Config::targetAimPlanEnabled;
        struct PlanGuard { bool previous; ~PlanGuard() { OW::Config::targetAimPlanEnabled=previous; OW::Config::selectedAimPlan={}; } } planGuard{previousPlanEnabled};
        OW::Config::ScopedLocalConfigOverride configOverride;
        OW::Config::RuntimeDrawFovState originalDraw = OW::Config::SnapshotRuntimeDrawFov();
        ScopedHeroPresetOverride(RuntimeState& state) {
            OW::Config::targetAimPlanEnabled=false; OW::Config::selectedAimPlan={}; OW::Config::selectedAimHero=0;
            const auto local = LocalEntity();
            RuntimePresetSelection selection{};
            if(!SelectAimPresetForActivation(state,local.HeroID,selection)) {
                OW::Config::targetAimPlanEnabled=true;return;
            }
            OW::Config::ApplyLocalHeroAimPreset(selection.preset);
            OW::Config::targetAimPlanEnabled=true;
            if (selection.activationKeyOverride >= 0) OW::Config::View_aim_key() = selection.activationKeyOverride;
            OW::Config::SetRuntimeDrawFov(OW::Config::ResolveRuntimeHeroPresetFovForDistance(selection.preset, 0.0f),
                static_cast<int>(OW::Config::FovRingSlotKind::Aim), selection.slotIndex);
            LogRuntimePresetSelection("aim", local.HeroID, selection);
        }
        ~ScopedHeroPresetOverride() {
            OW::Config::RestoreRuntimeDrawFov(originalDraw);
        }
    };
    struct ScopedHeroTriggerPresetOverride {
        bool previousPlanEnabled=OW::Config::targetAimPlanEnabled;
        struct PlanGuard { bool previous; ~PlanGuard() { OW::Config::targetAimPlanEnabled=previous; OW::Config::selectedAimPlan={}; } } planGuard{previousPlanEnabled};
        OW::Config::ScopedLocalConfigOverride configOverride;
        ScopedHeroTriggerPresetOverride() {
            OW::Config::targetAimPlanEnabled=false; OW::Config::selectedAimPlan={}; OW::Config::selectedAimHero=0;
            // Trigger owns preference/HP filtering, never Aim-only hero exceptions.
            OW::Config::View_aimPlan().groups.clear();
            OW::Config::View_aimPlan().overrideMotion=false;
            const auto local = LocalEntity();
            RuntimePresetSelection selection{};
            if (!local.HeroID || !TrySelectRuntimeTriggerPreset(local.HeroID, selection)) { OW::Config::targetAimPlanEnabled=true; return; }
            OW::Config::ApplyLocalHeroTriggerPreset(selection.preset);
            OW::Config::targetAimPlanEnabled=true;
            LogRuntimePresetSelection("trigger", local.HeroID, selection);
        }

    };



    bool IsZeroVector(const Vector3& value) {
        return value == Vector3(0, 0, 0);
    }

    inline Vector3 CameraPosition() {
        return OW::SnapshotCameraPosition();
    }

    inline OW::c_entity LocalEntity() {
        return OW::TargetingDetail::SnapshotLocalEntity();
    }

    inline void LogAimKeyState(
        int keySetting,
        int vk,
        KeyState::HotkeyInputSource source,
        bool pressed) {
        struct LastLogState {
            bool initialized = false;
            int keySetting = -1;
            int vk = -1;
            KeyState::HotkeyInputSource source = KeyState::HotkeyInputSource::DmaKeyState;
            bool pressed = false;
            bool sampleAvailable = false;
            size_t sampleByteIndex = 0;
            uint8_t sampleRawByte = 0;
            uint8_t sampleDownMask = 0;
            DWORD lastLogTick = 0;
        };

        static LastLogState state{};
        const KeyState::KeyStateVkSample sample =
            source == KeyState::HotkeyInputSource::DmaKeyState
                ? KeyState::SampleVk(vk)
                : KeyState::KeyStateVkSample{};

        const DWORD now = GetTickCount();
        const bool changed = !state.initialized ||
            state.keySetting != keySetting ||
            state.vk != vk ||
            state.source != source ||
            state.pressed != pressed ||
            state.sampleAvailable != sample.available ||
            state.sampleByteIndex != sample.byteIndex ||
            state.sampleRawByte != sample.rawByte ||
            state.sampleDownMask != sample.downMask;

        if (changed || (pressed && (state.lastLogTick == 0 || now - state.lastLogTick >= 1000))) {
            Diagnostics::Aim("hotkey state keySetting=%d vk=0x%X source=%s ready=%d addr=0x%llX pressed=%d sampleAvailable=%d byte[%zu]=0x%02X mask=0x%02X sampleDown=%d",
                keySetting,
                vk,
                KeyState::HotkeyInputSourceLabel(source),
                KeyState::IsDmaKeyStateReady() ? 1 : 0,
                static_cast<unsigned long long>(KeyState::gafAsyncKeyStateAddr.load(std::memory_order_acquire)),
                pressed ? 1 : 0,
                sample.available ? 1 : 0,
                sample.byteIndex,
                static_cast<unsigned>(sample.rawByte),
                static_cast<unsigned>(sample.downMask),
                sample.down ? 1 : 0);
            state.initialized = true;
            state.keySetting = keySetting;
            state.vk = vk;
            state.source = source;
            state.pressed = pressed;
            state.sampleAvailable = sample.available;
            state.sampleByteIndex = sample.byteIndex;
            state.sampleRawByte = sample.rawByte;
            state.sampleDownMask = sample.downMask;
            state.lastLogTick = now;
        }
    }

    inline bool ReadDmaKeyStateVk(int vk, int keySetting) {
        if (!KeyState::IsDmaKeyStateReady()) {
            LogAimKeyState(keySetting, vk, KeyState::HotkeyInputSource::DmaKeyState, false);
            return false;
        }

        const bool pressed = KeyState::IsKeyDown(static_cast<uint32_t>(vk));
        LogAimKeyState(keySetting, vk, KeyState::HotkeyInputSource::DmaKeyState, pressed);
        return pressed;
    }

    bool IsInputVkDown(int vk, int keySetting) {
        if (vk <= 0) {
            LogAimKeyState(
                keySetting,
                vk,
                KeyState::SelectHotkeyInputSource(KeyState::IsDmaKeyStateReady()),
                false);
            return false;
        }

        return ReadDmaKeyStateVk(vk, keySetting);
    }

    inline bool ReadDmaKeyStateVkQuiet(int vk) {
        if (!KeyState::IsDmaKeyStateReady())
            return false;
        return KeyState::IsKeyDown(static_cast<uint32_t>(vk));
    }

    inline bool IsInputVkDownQuiet(int vk) {
        if (vk <= 0)
            return false;

        return ReadDmaKeyStateVkQuiet(vk);
    }

    inline bool IsConfiguredActivationKeyPressedForSelection(int keySetting,
                                                             int* matchedKeySetting) {
        return OW::Labels::TryMatchAimActivationKey(
            keySetting,
            [](int vk) {
                return IsInputVkDownQuiet(vk);
            },
            matchedKeySetting);
    }

    inline bool IsConfiguredAimKeyPressed(int keySetting) {
        const int vk = OW::get_bind_id(keySetting);
        if (vk <= 0) {
            LogAimKeyState(
                keySetting,
                vk,
                KeyState::SelectHotkeyInputSource(KeyState::IsDmaKeyStateReady()),
                false);
            static DWORD lastInvalidBindingLogTick = 0;
            const DWORD now = GetTickCount();
            if (lastInvalidBindingLogTick == 0 || now - lastInvalidBindingLogTick >= 1000) {
                Diagnostics::Aim("hotkey early_return reason=invalid_binding keySetting=%d", keySetting);
                lastInvalidBindingLogTick = now;
            }
            return false;
        }

        return IsInputVkDown(vk, keySetting);
    }

    inline bool IsAimActivationKeyDown() {
        if (!OW::RuntimeWorkersRunning() || !OW::HasFreshViewMatrices())
            return false;
        const bool activationHotkeyDown = IsConfiguredAimKeyPressed(OW::Config::View_aim_key());
        const bool actionButtonDown = !OW::Config::View_aimbotRequireActionHeld() ||
            IsInputVkDownQuiet(PhysicalMouseVkForAction(OW::Config::View_aimbotAttack()));
        return OW::AimActivationAllowed(
            activationHotkeyDown,
            OW::Config::View_aimbotRequireActionHeld(),
            actionButtonDown);
    }

    inline bool IsAimKeyPressed() {
        if(UsesReleaseKeyUpExecution())
            return activeAimShotState && activeAimShotState->releasePending &&
                !activeAimShotState->releaseShotLatched && OW::RuntimeWorkersRunning() && OW::HasFreshViewMatrices();
        return IsAimActivationKeyDown();
    }

    inline bool IsSecondAimKeyPressed() {
        if (!OW::RuntimeWorkersRunning() || !OW::HasFreshViewMatrices())
            return false;
        return IsConfiguredAimKeyPressed(OW::Config::aim_key2);
    }

    inline bool HasEntitySnapshot() {
        return OW::TargetingDetail::HasPublishedEntities();
    }

    inline void MaintainSensitivity(float& origin_sens) {
        const uintptr_t sensitive_ptr = OW::GetSenstivePTR();
        if (!sensitive_ptr) return;
        const float current_sens = SDK->RPM<float>(sensitive_ptr);
        if (std::isfinite(current_sens) && current_sens > 0.0f) {
            origin_sens = current_sens;
        }
    }

    inline void SetSensitivityLocked(bool locked, float origin_sens) {
        (void)locked;
        (void)origin_sens;
    }

    void MoveAimDelta(const Vector3& current_angle, const Vector3& target_angle, int move_time_ms) {
        if (!OW::RuntimeWorkersRunning() || !OW::HasFreshViewMatrices())
            return;
        const Vector3 delta = target_angle - current_angle;
        const int effective_move_time_ms = move_time_ms < 0
            ? OW::Config::outputInputDelayMs
            : move_time_ms;
        Diagnostics::Aim("move.delta current=(%.9f,%.9f,%.9f) target=(%.9f,%.9f,%.9f) delta=(%.9f,%.9f,%.9f) delta_len=%.9f moveTimeMs=%d",
            current_angle.X,
            current_angle.Y,
            current_angle.Z,
            target_angle.X,
            target_angle.Y,
            target_angle.Z,
            delta.X,
            delta.Y,
            delta.Z,
            delta.Size(),
            effective_move_time_ms);
        OW::MouseMoveQuantizationOptions options{};
        const auto sent=OW::SendMouseMove(delta,effective_move_time_ms,options);
        const float yaw=OW::Config::OutputYawCountsPerRadian(), pitch=OW::Config::OutputPitchCountsPerRadian();
        const bool accepted=sent.dispatched && sent.status==0 && yaw>0 && pitch>0;
        OW::CommitAimMotionCommand(accepted ? Vector3(sent.pixelY/pitch,-sent.pixelX/yaw,0) : Vector3{});
    }

    inline OW::MouseMoveQuantizationResult MoveMagneticTriggerAimDelta(
        RuntimeState& state,
        const Vector3& currentAngle,
        const Vector3& outputAngle,
        const Vector3& rawTargetAngle,
        int moveTimeMs = -1) {
        if (!OW::RuntimeWorkersRunning() || !OW::HasFreshViewMatrices())
            return {};
        const Vector3 delta = outputAngle - currentAngle;
        const Vector3 rawDelta = rawTargetAngle - currentAngle;
        const int effectiveMoveTimeMs = moveTimeMs < 0
            ? OW::Config::outputInputDelayMs
            : moveTimeMs;
        if (OW::Config::aimVerboseLog) {
            Diagnostics::Aim("magnetic_trigger.move delta=(%.9f,%.9f,%.9f) raw_delta=(%.9f,%.9f,%.9f) moveTimeMs=%d",
                delta.X,
                delta.Y,
                delta.Z,
                rawDelta.X,
                rawDelta.Y,
                rawDelta.Z,
                effectiveMoveTimeMs);
        }

        OW::MouseMoveQuantizationOptions quantization{};
        quantization.state = &state.magneticMoveQuantization;
        quantization.fallbackDirectionDelta = rawDelta;
        quantization.forceNonZero = true;
        const auto sent=OW::SendMouseMove(delta,effectiveMoveTimeMs,quantization);
        const float yaw=OW::Config::OutputYawCountsPerRadian(), pitch=OW::Config::OutputPitchCountsPerRadian();
        const bool accepted=sent.dispatched && sent.status==0 && yaw>0 && pitch>0;
        OW::CommitAimMotionCommand(accepted ? Vector3(sent.pixelY/pitch,-sent.pixelX/yaw,0) : Vector3{});
        return sent;
    }

    inline constexpr const char* kGlobalAimClickOwnerKey =
        "overwatch.global_aim.click";
    inline constexpr const char* kGlobalAimTrackingFireOwnerKey =
        "overwatch.global_aim.tracking_fire";
    inline constexpr const char* kGlobalAimCloseRangeOwnerKey =
        "overwatch.global_aim.close_range";
    inline constexpr const char* kGlobalAimGeneratedFireOwnerKey =
        "overwatch.global_aim.generated_fire";
    inline constexpr const char* kTriggerGeneratedFireOwnerKey =
        "overwatch.trigger.generated_fire";
    inline constexpr const char* kTriggerCloseRangeOwnerKey =
        "overwatch.trigger.close_range";

    inline bool ClickMouseButton(
        int button,
        DWORD sleep_ms = 10,
        OW::OutputOwnerSource ownerSource = OW::OutputOwnerSource::GlobalAim,
        const char* ownerKey = kGlobalAimClickOwnerKey,
        bool requireNewPress = false, DWORD* submittedAt = nullptr) {
        if (!OW::RuntimeWorkersRunning() || !OW::HasFreshViewMatrices())
            return false;
        const std::uint64_t operationGeneration =
            V2::Output::ActiveRuntimeSnapshot().generation;
        const OW::ActionOutputStatus pressStatus =
            OW::SendMouseButtonActionState(
                button,
                true,
                ownerSource,
                ownerKey,
                operationGeneration,
                requireNewPress);
        if (!OW::ActionOutputSucceeded(pressStatus))
            return false;
        if (submittedAt) *submittedAt = GetTickCount();
        OW::RuntimeWorkerSleep(sleep_ms);
        (void)OW::SendMouseButtonActionState(
            button,
            false,
            ownerSource,
            ownerKey,
            operationGeneration);
        return true;
    }

    inline constexpr uint32_t PhysicalMouseMaskForButton(int button) {
        switch (button) {
        case 0: return 0x01u;
        case 1: return 0x02u;
        case 2: return 0x04u;
        default: return 0u;
        }
    }

    inline constexpr int VkForMouseButton(int button) {
        switch (button) {
        case 0: return VK_LBUTTON;
        case 1: return VK_RBUTTON;
        case 2: return VK_MBUTTON;
        default: return 0;
        }
    }

    inline bool ShouldHoldFireWhileTracking(int outputButton) {
        const FirePolicyType policy =
            ClampFirePolicy(OW::Config::View_aimbotFirePolicy());
        const int activationVk = OW::get_bind_id(OW::Config::View_aim_key());
        const int outputVk = VkForMouseButton(outputButton);
        const bool activationIsPrimaryMouseAction =
            activationVk == VK_LBUTTON ||
            activationVk == VK_RBUTTON ||
            activationVk == VK_MBUTTON;
        return OW::AllowsGeneratedTrackingHold(
            policy,
            activationIsPrimaryMouseAction,
            activationVk == outputVk);
    }

    inline bool GlobalAimFirePolicyAllowsGeneratedFire() {
        return AllowsGeneratedAimFire(ClampFirePolicy(OW::Config::View_aimbotFirePolicy()));
    }

    inline bool GeneratedFireAllowedForSource(OW::OutputOwnerSource ownerSource) {
        return ownerSource != OW::OutputOwnerSource::GlobalAim ||
            GlobalAimFirePolicyAllowsGeneratedFire();
    }

    inline void LogInvalidGeneratedFireKey(const char* operation, uint32_t key) {
        static std::atomic<DWORD> lastLogTick{ 0 };
        const DWORD now = GetTickCount();
        DWORD observed = lastLogTick.load(std::memory_order_relaxed);
        if (observed != 0 && now - observed < 1000)
            return;
        if (!lastLogTick.compare_exchange_strong(observed, now, std::memory_order_relaxed))
            return;
        Diagnostics::Warn("Generated fire key is not a mouse mask. operation=%s keyMask=0x%X",
            operation ? operation : "unknown",
            key);
    }

    inline bool ClickDmaMouseKey(
        uint32_t key,
        DWORD sleep_ms = 10,
        OW::OutputOwnerSource ownerSource = OW::OutputOwnerSource::GlobalAim,
        const char* ownerKey = kGlobalAimGeneratedFireOwnerKey,
        bool requireNewPress = false, DWORD* submittedAt = nullptr) {
        int button = -1;
        if (OW::DmaKeyToMouseButton(key, button)) {
            return ClickMouseButton(
                button,
                sleep_ms,
                ownerSource,
                ownerKey,
                requireNewPress, submittedAt);
        } else {
            LogInvalidGeneratedFireKey("click", key);
            return false;
        }
    }

    inline std::string PhysicalMouseMaskLeaseKey(
        OW::OutputOwnerSource ownerSource,
        const char* ownerKey,
        int button) {
        const std::string resolvedOwnerKey = ownerKey && *ownerKey
            ? std::string(ownerKey)
            : OW::DefaultMouseButtonOwnerKey(ownerSource, button);
        return resolvedOwnerKey + ".physical_mask." +
            std::to_string(button);
    }

    struct MouseOwnerReleaseResult {
        OW::ActionOutputStatus ownerReleaseStatus =
            OW::ActionOutputStatus::TransportError;
        bool physicalFallbackReleased = false;
    };

    inline MouseOwnerReleaseResult ReleaseMouseOwnerWithPhysicalFallback(
        int button,
        OW::OutputOwnerSource ownerSource,
        const char* ownerKey,
        std::uint64_t expectedGeneration) {
        MouseOwnerReleaseResult result{};
        result.ownerReleaseStatus =
            OW::ReleaseMouseButtonActionStateOrPhysicalFallback(
            button,
            ownerSource,
            ownerKey,
            expectedGeneration,
            &result.physicalFallbackReleased);
        return result;
    }

    inline MouseOwnerReleaseResult ReleaseMouseOwnerAfterPhysicalMask(
        int button,
        OW::OutputOwnerSource ownerSource,
        const char* ownerKey,
        std::uint64_t expectedGeneration,
        bool physicalDown,
        bool physicalMaskConfirmed) {
        if (!physicalDown || physicalMaskConfirmed) {
            return ReleaseMouseOwnerWithPhysicalFallback(
                button,
                ownerSource,
                ownerKey,
                expectedGeneration);
        }

        MouseOwnerReleaseResult result{};
        result.ownerReleaseStatus =
            OW::ReleaseMouseButtonActionStateForGeneration(
                button,
                ownerSource,
                ownerKey,
                expectedGeneration);
        return result;
    }

    inline void RecoverMouseAfterMaskedRelease(
        const char* reason,
        int button = 0,
        OW::OutputOwnerSource ownerSource = OW::OutputOwnerSource::GlobalAim,
        const char* ownerKey = kGlobalAimGeneratedFireOwnerKey,
        std::uint64_t expectedGeneration = 0) {
        const V2::Output::OutputRuntimeAppliedState applied =
            V2::Output::ActiveRuntimeSnapshot();
        const std::uint64_t operationGeneration = expectedGeneration != 0
            ? expectedGeneration
            : applied.generation;
        if (operationGeneration == 0 ||
            applied.generation != operationGeneration ||
            !V2::Output::SupportsMouseMask(applied.descriptor)) {
            return;
        }

        const std::string maskLeaseKey = PhysicalMouseMaskLeaseKey(
            ownerSource,
            ownerKey,
            button);
        const int vk = VkForMouseButton(button);
        const bool physicalDown = vk != 0 && IsInputVkDownQuiet(vk);
        const bool physicalMaskConfirmed =
            OW::IsPhysicalMouseMaskLeaseConfirmed(
                maskLeaseKey,
                ownerSource);
        Diagnostics::Aim("fire.recover_mouse reason=%s button=%d stage=owner_release physicalDown=%d maskConfirmed=%d",
            reason ? reason : "unknown",
            button,
            physicalDown ? 1 : 0,
            physicalMaskConfirmed ? 1 : 0);
        const MouseOwnerReleaseResult release =
            ReleaseMouseOwnerAfterPhysicalMask(
            button,
            ownerSource,
            ownerKey,
            operationGeneration,
            physicalDown,
            physicalMaskConfirmed);
        Diagnostics::Aim(
            "fire.recover_mouse ownerReleaseStatus=%d physicalFallbackReleased=%d",
            static_cast<int>(release.ownerReleaseStatus),
            release.physicalFallbackReleased ? 1 : 0);
        OW::RuntimeWorkerSleep(1);
        const bool maskReleased = OW::ReleasePhysicalMouseMaskLease(
            maskLeaseKey,
            ownerSource,
            operationGeneration);
        Diagnostics::Aim(
            "fire.recover_mouse mask_owner_release reason=%s ok=%d",
            reason ? reason : "unknown",
            maskReleased ? 1 : 0);
    }

    inline void RecoverHanzoPostFireMouseState(
        OW::OutputOwnerSource ownerSource = OW::OutputOwnerSource::GlobalAim,
        const char* ownerKey = kGlobalAimGeneratedFireOwnerKey,
        std::uint64_t expectedGeneration = 0) {
        const V2::Output::OutputRuntimeAppliedState applied =
            V2::Output::ActiveRuntimeSnapshot();
        const std::uint64_t operationGeneration = expectedGeneration != 0
            ? expectedGeneration
            : applied.generation;
        if (operationGeneration == 0 ||
            applied.generation != operationGeneration ||
            !V2::Output::SupportsMouseMask(applied.descriptor)) {
            return;
        }

        RecoverMouseAfterMaskedRelease(
            "hanzo_post_fire", 0, ownerSource, ownerKey, operationGeneration);
        OW::RuntimeWorkerSleep(8);
        RecoverMouseAfterMaskedRelease(
            "hanzo_post_fire_delayed", 0, ownerSource, ownerKey, operationGeneration);
    }

    inline bool ReleaseDmaMouseKey(
        uint32_t key,
        DWORD sleep_ms = 10,
        bool allowPhysicalMask = true,
        OW::OutputOwnerSource ownerSource = OW::OutputOwnerSource::GlobalAim,
        const char* ownerKey = kGlobalAimGeneratedFireOwnerKey,
        std::uint64_t expectedGeneration = 0, DWORD* submittedAt = nullptr) {
        const std::uint64_t operationGeneration = expectedGeneration != 0
            ? expectedGeneration
            : V2::Output::ActiveRuntimeSnapshot().generation;
        int button = -1;
        if (OW::DmaKeyToMouseButton(key, button)) {
            const uint32_t physicalMask = PhysicalMouseMaskForButton(button);
            const std::string maskLeaseKey = PhysicalMouseMaskLeaseKey(
                ownerSource,
                ownerKey,
                button);
            const int vk = VkForMouseButton(button);
            const bool physicalDown = vk != 0 && IsInputVkDownQuiet(vk);
            const bool masked = allowPhysicalMask &&
                physicalDown &&
                physicalMask != 0 &&
                OW::AcquirePhysicalMouseMaskLease(
                    maskLeaseKey,
                    ownerSource,
                    physicalMask,
                    operationGeneration);
            Diagnostics::Aim("fire.release_mouse keyMask=0x%X button=%d physicalMask=0x%02X physicalDown=%d masked=%d allowMask=%d",
                key,
                button,
                physicalMask,
                physicalDown ? 1 : 0,
                masked ? 1 : 0,
                allowPhysicalMask ? 1 : 0);
            if (masked)
                OW::RuntimeWorkerSleep(1);
            const MouseOwnerReleaseResult release =
                ReleaseMouseOwnerAfterPhysicalMask(
                    button,
                    ownerSource,
                    ownerKey,
                    operationGeneration,
                    physicalDown,
                    masked);
            Diagnostics::Aim(
                "fire.release_mouse ownerReleaseStatus=%d physicalFallbackReleased=%d",
                static_cast<int>(release.ownerReleaseStatus),
                release.physicalFallbackReleased ? 1 : 0);
            const bool accepted = OW::ActionOutputSucceeded(release.ownerReleaseStatus) ||
                release.physicalFallbackReleased;
            if (accepted && submittedAt) *submittedAt = GetTickCount();
            OW::RuntimeWorkerSleep(sleep_ms);
            if (masked) {
                const bool maskReleased =
                    OW::ReleasePhysicalMouseMaskLease(
                        maskLeaseKey,
                        ownerSource,
                        operationGeneration);
                Diagnostics::Aim(
                    "fire.release_mouse mask_owner_release ok=%d",
                    maskReleased ? 1 : 0);
            }
            return accepted;
        } else {
            LogInvalidGeneratedFireKey("charge_release", key);
            return false;
        }
    }

    inline FlickRepeat::SessionIdentity CurrentOrdinaryFlickIdentity();
    inline const char* ReleaseMaskOwner(int button) {
        static const char* keys[]={"aim.release.physical.left","aim.release.physical.right","aim.release.physical.middle"};
        return keys[(std::clamp)(button,0,2)];
    }
    inline bool ResetAimCharge(RuntimeState& state,bool requireSubmission) {
        if(!state.releaseOwnedDown) return true;
        const auto status=OW::ReleaseMouseButtonActionStateOrPhysicalFallback(state.releaseOwnedButton,
            OW::OutputOwnerSource::GlobalAim,kGlobalAimGeneratedFireOwnerKey,state.releaseOutputGeneration);
        if(!OW::ActionOutputSucceeded(status) &&
            V2::Output::ActiveRuntimeSnapshot().generation==state.releaseOutputGeneration) return false;
        state.releaseOwnedDown=false;state.releaseOwnedButton=-1;state.releaseChargeStarted=0;return !requireSubmission||OW::ActionOutputSucceeded(status);
    }
    inline void CancelConfiguredAimShot(RuntimeState& state) {
        state.releasePending=false;state.releaseNeedsActivationUp=true;
        if(ResetAimCharge(state)) chargedAimPreset.reset();
    }
    inline void ObserveReleaseFireInput(RuntimeState& state,bool cleanup=false) {
        const auto generation=V2::Output::ActiveRuntimeSnapshot().generation;
        const auto physical=V2::Output::ActivePhysicalInput();
        for(int button=0;button<3;++button) {
            auto& press=state.releasePresses[button];
            const bool down=physical.available ? (physical.buttons&PhysicalMouseMaskForButton(button))!=0 : press.down;
            if(press.masked && (!down||!press.consumed||cleanup||press.generation!=generation)) {
                if(OW::ReleasePhysicalMouseMaskLease(ReleaseMaskOwner(button),OW::OutputOwnerSource::GlobalAim,press.generation)
                    || press.generation!=generation) press.masked=false;
            }
            if(physical.available) press.Observe(down,GetTickCount());
        }
        if(cleanup) CancelConfiguredAimShot(state);
    }
    inline bool AimModeStructureCurrent() {
        const int id=OW::Config::View_aimBehaviorPresetId();if(id<0) return true;
        const auto latest=OW::Config::SnapshotConfigValues();
        for(const auto& mode:latest->aimBehaviorPresets) if(mode.id==id)
            return mode.behavior==OW::Config::View_aimBehavior() && mode.method==OW::Config::View_aimModeState().motion.method;
        return false;
    }
    inline void MaintainConfiguredAimShot(RuntimeState& state) {
        ObserveReleaseFireInput(state);
        const auto session=CurrentOrdinaryFlickIdentity();
        const auto* tuning=OW::Config::ActiveSlotBehaviorTuning();
        const int mode=OW::Config::View_aimBehaviorPresetId(),behavior=OW::Config::View_aimBehavior();
        const int method=OW::Config::View_aimModeState().motion.method;
        const bool release=tuning&&tuning->releaseToFire,automatic=tuning&&tuning->autoCharge;
        const bool afterKeyUp=UsesReleaseKeyUpExecution();
        const bool rawDown=IsAimActivationKeyDown();
        const bool changed=state.releaseIdentified && (state.releaseSession!=session||state.releaseMode!=mode||
            state.releaseBehavior!=behavior||state.releaseMethod!=method||state.releaseConfigured!=release||
            state.releaseAutomatic!=automatic||state.releaseAfterKeyUp!=afterKeyUp||
            (state.releaseOwnedDown&&state.releaseOutputGeneration!=V2::Output::ActiveRuntimeSnapshot().generation));
        if(changed) {
            CancelConfiguredAimShot(state);state.releaseShotLatched=false;
        }
        const bool keySampleAvailable=KeyState::SampleVk(OW::get_bind_id(OW::Config::View_aim_key())).available;
        if(!keySampleAvailable) {
            CancelConfiguredAimShot(state);
        } else if(!rawDown && !state.releasePending) {
            if(afterKeyUp && state.releaseActivationDown && state.releaseOwnedDown &&
                !changed && !state.releaseNeedsActivationUp) {
                state.releasePending=true;state.releaseActionStarted=GetTickCount();
                state.releaseAimReportTick=0;state.releaseChargeWaitLogged=false;
                state.releaseShotLatched=false;OW::Config::shooted=false;
                OW::ResetAimSmoothingState();
                Diagnostics::Info("[V2 shot] release.execute mode=%d slot=%d charge=%.2f chargeMs=%lu",mode,session.slotIndex,
                    OW::ReadWeaponChargePercent(LocalEntity(),OW::Config::View_aimbotAttack()),
                    static_cast<unsigned long>(GetTickCount()-state.releaseChargeStarted));
            } else {
                if(ResetAimCharge(state)) chargedAimPreset.reset();
                state.releaseShotLatched=false;state.releaseNeedsActivationUp=false;
            }
        } else if(rawDown && !state.releaseActivationDown && !state.releasePending) {
            state.releaseShotLatched=false;
        }
        if(keySampleAvailable) state.releaseActivationDown=rawDown;
        state.releaseIdentified=true;state.releaseSession=session;state.releaseMode=mode;
        state.releaseBehavior=behavior;state.releaseMethod=method;state.releaseConfigured=release;
        state.releaseAutomatic=automatic;state.releaseAfterKeyUp=afterKeyUp;
    }
    inline bool PrepareAimCharge(RuntimeState& state) {
        ObserveReleaseFireInput(state);
        const auto* tuning=OW::Config::ActiveSlotBehaviorTuning();
        if(!tuning||!tuning->releaseToFire||!tuning->autoCharge) return true;
        const auto runtime=V2::Output::ActiveRuntimeSnapshot();
        if(OW::Config::aimDryRun||!OW::CurrentOutputRuntimeState().outputGateOpen||!OW::RuntimeWorkersRunning()||!OW::HasFreshViewMatrices()||
           (!IsAimActivationKeyDown()&&!state.releasePending)||!GlobalAimFirePolicyAllowsGeneratedFire()||OW::Config::reloading||
           state.releaseNeedsActivationUp||!AimModeStructureCurrent()||
           OW::ShouldBlockForActiveSequence(ExecutionSource::GlobalAim)) {ResetAimCharge(state);return false;}
        if(state.releaseShotLatched&&(UsesReleaseKeyUpExecution()||!OW::Config::View_aimbotKeepFiring())) return false;
        if(state.releasePending&&!state.releaseOwnedDown) return false;
        const auto local=LocalEntity();const auto* weapon=OW::ResolveWeaponSpec(local.HeroID,OW::Config::View_aimbotAttack());
        const int button=OW::ResolveGeneratedFireMouseButton(weapon,OW::Config::View_aimbotAttack());
        const auto physical=V2::Output::ActivePhysicalInput();
        const bool physicallyHeld=physical.available ? (physical.buttons&PhysicalMouseMaskForButton(button))!=0 :
            (!state.releaseOwnedDown && IsInputVkDownQuiet(VkForMouseButton(button)));
        if(button<0||button>2||OW::get_bind_id(OW::Config::View_aim_key())==VkForMouseButton(button)) return false;
        if(physicallyHeld) {ResetAimCharge(state);state.releaseNeedsActivationUp=true;return false;}
        if(state.releaseOwnedDown) return state.releaseOwnedButton==button&&state.releaseOutputGeneration==runtime.generation;
        const auto status=OW::SendMouseButtonActionState(button,true,OW::OutputOwnerSource::GlobalAim,
            kGlobalAimGeneratedFireOwnerKey,runtime.generation);
        if(!OW::ActionOutputSucceeded(status)) return false;
        state.releaseOwnedDown=true;state.releaseOwnedButton=button;
        state.releaseChargeStarted=GetTickCount();state.releaseOutputGeneration=runtime.generation;
        Diagnostics::Info("[V2 shot] charge.begin button=%d mode=%d",button,state.releaseMode);return true;
    }
    inline bool SubmitConfiguredAimShot(OW::OutputOwnerSource source,const char* owner,bool requireNewPress,DWORD* submittedAt,
                                        bool releaseAtCrosshair=false) {
        if(!activeAimShotState||!OW::RuntimeWorkersRunning()||!OW::HasFreshViewMatrices()||
           OW::Config::aimDryRun||!GeneratedFireAllowedForSource(source)||!AimModeStructureCurrent()) return false;
        auto& state=*activeAimShotState;ObserveReleaseFireInput(state);
        if(state.releaseNeedsActivationUp) return false;
        const auto local=LocalEntity();const auto* weapon=OW::ResolveWeaponSpec(local.HeroID,OW::Config::View_aimbotAttack());
        const auto* tuning=OW::Config::ActiveSlotBehaviorTuning();
        const auto key=OW::ResolveGeneratedFireKeyMask(weapon,OW::Config::View_aimbotAttack());
        const int behavior=OW::Config::View_aimBehavior();
        if(!tuning||!tuning->releaseToFire || (behavior!=1&&behavior!=2&&behavior!=4))
            return ClickDmaMouseKey(key,10,source,owner,requireNewPress,submittedAt);
        const int button=OW::ResolveGeneratedFireMouseButton(weapon,OW::Config::View_aimbotAttack());
        if(button<0||button>2) return false;
        const auto runtime=V2::Output::ActiveRuntimeSnapshot();bool accepted=false;
        if(tuning->autoCharge) {
            if(UsesReleaseKeyUpExecution()&&!state.releasePending) return false;
            if(!PrepareAimCharge(state)) return false;
            const float charge=OW::ReadWeaponChargePercent(local,OW::Config::View_aimbotAttack());
            if(!releaseAtCrosshair && !OW::AimReleaseFire::ChargeReady(charge,
                tuning->minCharge,GetTickCount()-state.releaseChargeStarted,tuning->chargeFallbackMs)) {
                if(state.releasePending&&!state.releaseChargeWaitLogged) {
                    state.releaseChargeWaitLogged=true;
                    Diagnostics::Info("[V2 shot] release.wait_charge value=%.2f minimum=%.2f chargeMs=%lu fallbackMs=%.0f",
                        charge,tuning->minCharge,static_cast<unsigned long>(GetTickCount()-state.releaseChargeStarted),tuning->chargeFallbackMs);
                }
                return false;
            }
            accepted=ResetAimCharge(state,true);
        } else {
            auto& press=state.releasePresses[button];
            if(!press.Ready()||!V2::Output::ActivePhysicalInput().available||!V2::Output::SupportsMouseMask(runtime.descriptor)||
               !OW::AimReleaseFire::ChargeReady(OW::ReadWeaponChargePercent(local,OW::Config::View_aimbotAttack()),
                  tuning->minCharge,GetTickCount()-press.started,tuning->chargeFallbackMs)) return false;
            if(!OW::AcquirePhysicalMouseMaskLease(ReleaseMaskOwner(button),source,PhysicalMouseMaskForButton(button),runtime.generation)) return false;
            press.masked=true;press.generation=runtime.generation;
            OW::RuntimeWorkerSleep(1);
            const auto result=ReleaseMouseOwnerAfterPhysicalMask(button,source,owner,runtime.generation,true,true);
            accepted=OW::ActionOutputSucceeded(result.ownerReleaseStatus)||result.physicalFallbackReleased;
            press.Submitted(accepted);
            if(!accepted && OW::ReleasePhysicalMouseMaskLease(ReleaseMaskOwner(button),source,runtime.generation)) press.masked=false;
        }
        if(accepted) {
            state.releaseShotLatched=true;if(submittedAt) *submittedAt=GetTickCount();
            if(state.releasePending) {
                state.releasePending=false;chargedAimPreset.reset();
                state.releaseNeedsActivationUp=IsAimActivationKeyDown();
            }
            Diagnostics::Info("[V2 shot] submitted action=release automatic=%d button=%d mode=%d",tuning->autoCharge?1:0,button,state.releaseMode);
        }
        return accepted;
    }

    inline bool ShouldReleaseChargedFire(const OW::c_entity& local, const OW::WeaponSpec* weapon) {
        if (!weapon || weapon->firePolicy.type != OW::FirePolicyType::ChargeRelease)
            return false;

        return !(local.HeroID == OW::eHero::HERO_HANJO && local.skill2act);
    }

    inline bool ShouldSkipChargeReleaseForHeldActivation(const OW::c_entity& local,
                                                         const OW::WeaponSpec* weapon,
                                                         uint32_t fireKey,
                                                         const char* source) {
        if (!ShouldReleaseChargedFire(local, weapon))
            return false;

        int fireButton = -1;
        if (!OW::DmaKeyToMouseButton(fireKey, fireButton))
            return false;

        const int fireVk = VkForMouseButton(fireButton);
        const int activationVk = OW::get_bind_id(OW::Config::View_aim_key());
        if (fireVk <= 0 || activationVk != fireVk)
            return false;

        if (!IsInputVkDownQuiet(fireVk))
            return false;

        Diagnostics::Aim("fire.charge_release skip reason=held_activation_passthrough source=%s hero=0x%llX action=%d weapon=%s keyMask=0x%X button=%d aimKey=%d vk=0x%X",
            source ? source : "unknown",
            static_cast<unsigned long long>(local.HeroID),
            OW::Config::View_aimbotAttack(),
            weapon ? weapon->weaponId.data() : "none",
            fireKey,
            fireButton,
            OW::Config::View_aim_key(),
            fireVk);
        return true;
    }

    inline void FireConfiguredAction(
        const OW::c_entity& local,
        DWORD sleep_ms = 10,
        OW::OutputOwnerSource ownerSource = OW::OutputOwnerSource::GlobalAim,
        const char* ownerKey = kGlobalAimGeneratedFireOwnerKey) {
        if (!OW::RuntimeWorkersRunning() || !OW::HasFreshViewMatrices())
            return;
        if (!GeneratedFireAllowedForSource(ownerSource))
            return;
        const OW::WeaponSpec* weapon = OW::ResolveWeaponSpec(local.HeroID, OW::Config::View_aimbotAttack());
        const uint32_t fireKey = OW::ResolveGeneratedFireKeyMask(weapon, OW::Config::View_aimbotAttack());
        if (fireKey == 0)
            return;

        if (ShouldReleaseChargedFire(local, weapon)) {
            if (ShouldSkipChargeReleaseForHeldActivation(local, weapon, fireKey, "configured"))
                return;
            Diagnostics::Aim("fire.configured mode=charge_release hero=0x%llX action=%d weapon=%s keyMask=0x%X",
                static_cast<unsigned long long>(local.HeroID),
                OW::Config::View_aimbotAttack(),
                weapon ? weapon->weaponId.data() : "none",
                fireKey);
            ReleaseDmaMouseKey(
                fireKey, sleep_ms, true, ownerSource, ownerKey);
            return;
        }

        ClickDmaMouseKey(fireKey, sleep_ms, ownerSource, ownerKey);
    }

    inline void PressWithSensitivity(
        uint32_t key,
        float origin_sens,
        DWORD sleep_ms = 1,
        OW::OutputOwnerSource ownerSource = OW::OutputOwnerSource::GlobalAim,
        const char* ownerKey = kGlobalAimGeneratedFireOwnerKey) {
        if (key == 0)
            return;
        SetSensitivityLocked(true, origin_sens);
        ClickDmaMouseKey(key, sleep_ms, ownerSource, ownerKey);
        SetSensitivityLocked(false, origin_sens);
    }

    inline void ClickConfiguredFire(
        DWORD sleep_ms = 10,
        OW::OutputOwnerSource ownerSource = OW::OutputOwnerSource::GlobalAim,
        const char* ownerKey = kGlobalAimGeneratedFireOwnerKey) {
        FireConfiguredAction(LocalEntity(), sleep_ms, ownerSource, ownerKey);
    }

    inline bool CurrentTarget(c_entity& target, bool requireVisible = false) {
        return OW::TryGetTargetEntity(OW::Config::Targetenemyi, target, requireVisible);
    }

    bool IsPrimaryTargetActionable(c_entity& target) {
        if (!CurrentTarget(target)) return false;
        if (!OW::TargetingDetail::IsRuntimeTargetValid(target, false)) return false;
        if (target.skill2act && target.HeroID == OW::eHero::HERO_GENJI) return false;
        if (target.skill1act && target.HeroID == OW::eHero::HERO_VENTURE) return false;
        if ((target.imort || target.barrprot) && !OW::Config::switch_team) return false;
        return true;
    }

    inline bool IsPrimaryTargetSnapshotActionable(const c_entity& target) {
        if (!OW::TargetingDetail::IsRuntimeTargetValid(target, false)) return false;
        if (target.skill2act && target.HeroID == OW::eHero::HERO_GENJI) return false;
        if (target.skill1act && target.HeroID == OW::eHero::HERO_VENTURE) return false;
        if ((target.imort || target.barrprot) && !OW::Config::switch_team) return false;
        return true;
    }

    inline void ResetTrackingTargetContinuity(RuntimeState& state,
                                              const char* reason) {
        const uint64_t previousTargetKey = state.trackingTargetKey;
        const uint64_t previousActorKey = state.trackingTargetActorKey;
        state.trackingTargetKey = 0;
        state.trackingTargetActorKey = 0;
        state.trackingTargetMissingSinceTick = 0;
        state.trackingInDeadzone = false;
        OW::ResetAimSmoothingState();
        OW::ResetMouseMoveQuantizationState(
            OW::DefaultMouseMoveQuantizationState());
        if (previousTargetKey != 0 || previousActorKey != 0) {
            Diagnostics::Aim(
                "tracking.target reset reason=%s previousKey=0x%llX previousActor=0x%llX",
                reason ? reason : "unknown",
                static_cast<unsigned long long>(previousTargetKey),
                static_cast<unsigned long long>(previousActorKey));
        }
    }

    inline void PrepareTrackingTargetContinuity(RuntimeState& state,
                                                uint64_t targetKey,
                                                uint64_t actorKey) {
        if (targetKey == 0 && actorKey == 0)
            return;
        if (targetKey == 0)
            targetKey = actorKey;
        if (state.trackingTargetKey == targetKey &&
            state.trackingTargetActorKey == actorKey)
            return;

        const uint64_t previousTargetKey = state.trackingTargetKey;
        const uint64_t previousActorKey = state.trackingTargetActorKey;
        OW::HoldAimSmoothingState();
        OW::ResetMouseMoveQuantizationState(
            OW::DefaultMouseMoveQuantizationState());
        state.trackingTargetKey = targetKey;
        state.trackingTargetActorKey = actorKey;
        state.trackingTargetMissingSinceTick = 0;
        state.trackingInDeadzone = false;
        Diagnostics::Aim(
            "tracking.target change previousKey=0x%llX targetKey=0x%llX previousActor=0x%llX actor=0x%llX",
            static_cast<unsigned long long>(previousTargetKey),
            static_cast<unsigned long long>(targetKey),
            static_cast<unsigned long long>(previousActorKey),
            static_cast<unsigned long long>(actorKey));
    }

    inline bool HoldTrackingTargetDropout(RuntimeState& state,
                                          DWORD nowTick) {
        if (state.trackingTargetKey == 0)
            return false;
        if (state.trackingTargetMissingSinceTick == 0) {
            state.trackingTargetMissingSinceTick = nowTick;
            OW::HoldAimSmoothingState();
            OW::ResetMouseMoveQuantizationState(
                OW::DefaultMouseMoveQuantizationState());
        } else {
            OW::PrimeAimSmoothingClock();
        }
        if (nowTick - state.trackingTargetMissingSinceTick <=
            OW::TrackingContinuityPolicy::kTargetDropoutGraceMs) {
            return true;
        }
        ResetTrackingTargetContinuity(state, "target_dropout_timeout");
        return false;
    }

    inline Vector3 TrackingSampleAgeCompensatedAimPoint(
        const OW::TargetCandidate& candidate,
        DWORD nowTick) {
        Vector3 target = candidate.aimPoint;
        if (!candidate.valid || IsZeroVector(target) ||
            candidate.motion.kind == OW::EntityMotionState::Kind::TeleportOrInvalid) {
            return target;
        }

        const Vector3 velocity = candidate.motion.worldVelocity;
        if (!std::isfinite(velocity.X) || !std::isfinite(velocity.Y) ||
            !std::isfinite(velocity.Z)) {
            return target;
        }
        const float speed = velocity.Size();
        if (!std::isfinite(speed) || speed <= 0.01f || speed > 75.0f)
            return target;

        const DWORD sampleTick = candidate.entitySnapshot.position_sample_tick_ms != 0
            ? candidate.entitySnapshot.position_sample_tick_ms
            : candidate.entitySnapshot.render_sample_tick_ms;
        const float leadSeconds =
            OW::TrackingContinuityPolicy::FreshPositionSampleLeadSeconds(
                nowTick,
                sampleTick);
        Vector3 offset = velocity * leadSeconds;
        constexpr float kMaxTrackingPresentationOffsetMeters = 0.25f;
        const float offsetLength = offset.Size();
        if (!std::isfinite(offsetLength) || offsetLength <= 0.0f)
            return target;
        if (offsetLength > kMaxTrackingPresentationOffsetMeters) {
            offset = offset *
                (kMaxTrackingPresentationOffsetMeters / offsetLength);
        }
        return target + offset;
    }

    inline bool IsTriggerTargetActionable() {
        c_entity target{};
        if (!CurrentTarget(target)) return false;

        c_entity local = LocalEntity();
        if (target.skill2act &&
            target.HeroID == OW::eHero::HERO_GENJI &&
            !target.SameTeamAs(local)) {
            return false;
        }
        return true;
    }

    struct TwoStageScreenBox {
        float left = 0.0f;
        float top = 0.0f;
        float right = 0.0f;
        float bottom = 0.0f;
        int points = 0;
    };

    struct TwoStageRuntime {
        uint64_t entityKey = 0;
        bool torsoReached = false;
    };

    struct TwoStageAimPlan {
        Vector3 target{};
        bool active = false;
        bool triggerOpen = true;
        bool innerStage = false;
        int methodOverride = -1;
        float smoothScale = 1.0f;
    };

    inline TwoStageRuntime& TwoStageState() {
        static TwoStageRuntime state{};
        return state;
    }

    inline uint64_t TwoStageEntityKey(const c_entity& target) {
        if (target.address)
            return OW::PresentationTiming::ActorControlKey(target.address,target.actor_generation);
        if (target.LinkBase)
            return target.LinkBase;
        return target.roster_key;
    }

    inline void ResetTwoStageState() {
        TwoStageState() = TwoStageRuntime{};
    }

    inline bool ProjectAimPoint(const Vector3& world, Vector2& screen) {
        if (IsZeroVector(world))
            return false;
        const OW::Matrix view = OW::SnapshotViewMatrix();
        return view.WorldToScreen(world, &screen, Vector2(OW::WX, OW::WY), false);
    }

    inline void ExpandTwoStageBox(TwoStageScreenBox& box, const Vector2& point) {
        if (box.points == 0) {
            box.left = box.right = point.X;
            box.top = box.bottom = point.Y;
        } else {
            box.left = (std::min)(box.left, point.X);
            box.right = (std::max)(box.right, point.X);
            box.top = (std::min)(box.top, point.Y);
            box.bottom = (std::max)(box.bottom, point.Y);
        }
        ++box.points;
    }

    inline bool AddWorldPointToTwoStageBox(TwoStageScreenBox& box, const Vector3& world) {
        Vector2 screen{};
        if (!ProjectAimPoint(world, screen))
            return false;
        ExpandTwoStageBox(box, screen);
        return true;
    }

    inline bool TryBuildUpperBodyBox(c_entity target, TwoStageScreenBox& box) {
        box = TwoStageScreenBox{};
        AddWorldPointToTwoStageBox(box, target.head_pos);
        AddWorldPointToTwoStageBox(box, target.neck_pos);
        AddWorldPointToTwoStageBox(box, target.chest_pos);
        AddWorldPointToTwoStageBox(box, OW::TargetingDetail::CachedSkeletonBoneRawPosition(target,OW::BONE_R_SHOULDER));
        AddWorldPointToTwoStageBox(box, OW::TargetingDetail::CachedSkeletonBoneRawPosition(target,OW::BONE_L_SHOULDER));
        AddWorldPointToTwoStageBox(box, OW::TargetingDetail::CachedSkeletonBoneRawPosition(target,OW::BONE_PELVIS));

        if (box.points < 2)
            return false;

        const float padding = std::clamp(OW::Config::View_aimbotFlick2ndBoxPadding(), 0.0f, 80.0f);
        box.left -= padding;
        box.top -= padding;
        box.right += padding;
        box.bottom += padding;
        return box.left <= box.right && box.top <= box.bottom;
    }

    inline bool PointInsideTwoStageBox(const TwoStageScreenBox& box, const Vector2& point) {
        return box.points >= 2 &&
            point.X >= box.left &&
            point.X <= box.right &&
            point.Y >= box.top &&
            point.Y <= box.bottom;
    }

    inline float DistanceToTwoStageBox(const TwoStageScreenBox& box, const Vector2& point) {
        const float clampedX = std::clamp(point.X, box.left, box.right);
        const float clampedY = std::clamp(point.Y, box.top, box.bottom);
        const float dx = point.X - clampedX;
        const float dy = point.Y - clampedY;
        return sqrtf(dx * dx + dy * dy);
    }

    inline void ConsiderTwoStagePoint(const Vector3& world,
                                      const Vector2& crosshair,
                                      Vector3& bestWorld,
                                      float& bestDistance) {
        Vector2 screen{};
        if (!ProjectAimPoint(world, screen))
            return;
        const float distance = crosshair.Distance(screen);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestWorld = world;
        }
    }

    inline bool TryNearestUpperBodyPoint(c_entity target,
                                         bool prediction,
                                         Vector3& outTarget,
                                         float& outScreenDistance) {
        const Vector2 crosshair = OW::TargetingDetail::CrosshairCenter();
        Vector3 bestWorld{};
        float bestDistance = (std::numeric_limits<float>::max)();

        ConsiderTwoStagePoint(target.head_pos, crosshair, bestWorld, bestDistance);
        ConsiderTwoStagePoint(target.neck_pos, crosshair, bestWorld, bestDistance);
        ConsiderTwoStagePoint(target.chest_pos, crosshair, bestWorld, bestDistance);
        ConsiderTwoStagePoint(OW::TargetingDetail::CachedSkeletonBoneRawPosition(target,OW::BONE_R_SHOULDER), crosshair, bestWorld, bestDistance);
        ConsiderTwoStagePoint(OW::TargetingDetail::CachedSkeletonBoneRawPosition(target,OW::BONE_L_SHOULDER), crosshair, bestWorld, bestDistance);
        ConsiderTwoStagePoint(OW::TargetingDetail::CachedSkeletonBoneRawPosition(target,OW::BONE_PELVIS), crosshair, bestWorld, bestDistance);

        if (IsZeroVector(bestWorld))
            return false;

        outTarget = OW::TargetingDetail::ApplyPrediction(target, bestWorld, prediction, false);
        outScreenDistance = bestDistance;
        return true;
    }

    inline Vector3 ResolveHeadAimPoint(c_entity target, bool prediction, const Vector3& fallback) {
        Vector3 head = !IsZeroVector(target.head_pos) ? target.head_pos : target.neck_pos;
        if (IsZeroVector(head))
            head = fallback;
        return OW::TargetingDetail::ApplyPrediction(target, head, prediction, false);
    }

    inline bool TwoStageTriggerOpenForTarget(c_entity target) {
        if (!OW::Config::IsFlick2ndBehavior(OW::Config::View_aimBehavior()) ||
            !OW::Config::View_aimbotFlick2ndTriggerGate())
            return true;

        const uint64_t entityKey = TwoStageEntityKey(target);
        if (entityKey == 0)
            return false;

        TwoStageRuntime& state = TwoStageState();
        if (state.entityKey != entityKey) {
            state.entityKey = entityKey;
            state.torsoReached = false;
        }

        TwoStageScreenBox box{};
        const Vector2 crosshair = OW::TargetingDetail::CrosshairCenter();
        if (!TryBuildUpperBodyBox(target, box))
            return true;

        if (PointInsideTwoStageBox(box, crosshair))
            state.torsoReached = true;

        return state.torsoReached;
    }

    inline TwoStageAimPlan ResolveTwoStageAimPlan(const Vector3& defaultTarget,
                                                  c_entity target,
                                                  bool prediction) {
        TwoStageAimPlan plan{};
        plan.target = defaultTarget;
        if (!OW::Config::IsFlick2ndBehavior(OW::Config::View_aimBehavior()))
            return plan;

        const uint64_t entityKey = TwoStageEntityKey(target);
        if (entityKey == 0)
            return plan;

        TwoStageRuntime& state = TwoStageState();
        if (state.entityKey != entityKey) {
            state.entityKey = entityKey;
            state.torsoReached = false;
        }

        const Vector2 crosshair = OW::TargetingDetail::CrosshairCenter();
        TwoStageScreenBox box{};
        const bool hasBox = TryBuildUpperBodyBox(target, box);
        if (!hasBox) {
            Diagnostics::Aim("two_stage fallback key=0x%llX reason=no_upper_body_box",
                static_cast<unsigned long long>(entityKey));
            return plan;
        }

        const bool insideBox = hasBox && PointInsideTwoStageBox(box, crosshair);
        if (insideBox)
            state.torsoReached = true;

        Vector2 headScreen{};
        const bool hasHeadScreen = ProjectAimPoint(target.head_pos, headScreen);
        const float headDistance = hasHeadScreen
            ? crosshair.Distance(headScreen)
            : (std::numeric_limits<float>::max)();
        const float innerRadius = std::clamp(OW::Config::View_aimbotFlick2ndInnerRadius(), 0.0f, 250.0f);
        const bool innerStage = state.torsoReached || headDistance <= innerRadius;

        plan.active = true;
        plan.triggerOpen = !OW::Config::View_aimbotFlick2ndTriggerGate() || state.torsoReached;
        plan.innerStage = innerStage;
        plan.methodOverride = innerStage
            ? OW::Config::ClampAimMethodOverride(OW::Config::View_aimbotFlick2ndInnerMethod())
            : -1;
        plan.smoothScale = innerStage
            ? std::clamp(OW::Config::View_aimbotFlick2ndInnerSmoothScale(), 0.1f, 1.0f)
            : 1.0f;

        float bodyDistance = 0.0f;
        Vector3 bodyTarget{};
        const bool hasBodyTarget = TryNearestUpperBodyPoint(target, prediction, bodyTarget, bodyDistance);
        plan.target = innerStage
            ? ResolveHeadAimPoint(target, prediction, defaultTarget)
            : (hasBodyTarget ? bodyTarget : defaultTarget);

        const float boxDistance = hasBox
            ? DistanceToTwoStageBox(box, crosshair)
            : (std::numeric_limits<float>::max)();
        Diagnostics::Aim("two_stage plan key=0x%llX active=1 insideBox=%d torsoReached=%d triggerOpen=%d inner=%d boxDist=%.3f headDist=%.3f bodyDist=%.3f method=%d smoothScale=%.3f target=(%.9f,%.9f,%.9f)",
            static_cast<unsigned long long>(entityKey),
            insideBox ? 1 : 0,
            state.torsoReached ? 1 : 0,
            plan.triggerOpen ? 1 : 0,
            plan.innerStage ? 1 : 0,
            boxDistance,
            headDistance,
            hasBodyTarget ? bodyDistance : -1.0f,
            plan.methodOverride,
            plan.smoothScale,
            plan.target.X,
            plan.target.Y,
            plan.target.Z);
        return plan;
    }

    AimData BuildAimData(const Vector3& world_target,
                                bool accelerated,
                                float smooth,
                                float acceleration,
                                int methodOverride,
                                float bezierSpeedOverride,
                                bool commitSmoothingOutput,
                                bool calculateSmoothing,
                                bool inheritTargetMotion) {
        AimData data{};
        const uint64_t playerControllerBase = SDK->g_player_controller;
        if (!playerControllerBase) {
            Diagnostics::Aim("angle.missing playerControllerBase");
            return data;
        }

        OW::Matrix aimViewMatrix{}, aimViewMatrixXor{};
        OW::GetViewMatricesSnapshot(aimViewMatrix, aimViewMatrixXor);
        const XMFLOAT3 cameraPos = aimViewMatrixXor.get_location();
        const XMFLOAT3 cameraForward = aimViewMatrixXor.get_rotation();

        // PlayerController + 0x1260 stores a normalized camera forward direction vector (fx, fy, fz).
        const uint64_t viewDirAddress = playerControllerBase + OW::kPlayerControllerViewDirectionOffset;
        Vector3 viewDir = OW::ReadPlayerControllerViewDirection(playerControllerBase);
        float dirLen = sqrtf(viewDir.X * viewDir.X + viewDir.Y * viewDir.Y + viewDir.Z * viewDir.Z);
        const float matrixForwardLen = sqrtf(cameraForward.x * cameraForward.x +
                                             cameraForward.y * cameraForward.y +
                                             cameraForward.z * cameraForward.z);
        const bool memoryViewDirValid =
            std::isfinite(viewDir.X) &&
            std::isfinite(viewDir.Y) &&
            std::isfinite(viewDir.Z) &&
            dirLen > 0.5f &&
            dirLen < 1.5f;
        const bool matrixForwardValid =
            std::isfinite(cameraForward.x) &&
            std::isfinite(cameraForward.y) &&
            std::isfinite(cameraForward.z) &&
            matrixForwardLen > 0.5f &&
            matrixForwardLen < 1.5f;
        const char* viewDirSource = "player_controller+0x1260";
        if (!memoryViewDirValid && matrixForwardValid) {
            viewDir = Vector3(cameraForward.x, cameraForward.y, cameraForward.z);
            dirLen = matrixForwardLen;
            viewDirSource = "view_matrix_forward_fallback";
        }
        if (!memoryViewDirValid && !matrixForwardValid) {
            Diagnostics::Aim("angle.missing_valid_viewdir playerControllerBase=0x%llX viewDirAddress=0x%llX mem=(%.6f,%.6f,%.6f) mem_len=%.4f matrix=(%.6f,%.6f,%.6f) matrix_len=%.4f",
                static_cast<unsigned long long>(playerControllerBase),
                static_cast<unsigned long long>(viewDirAddress),
                viewDir.X,
                viewDir.Y,
                viewDir.Z,
                dirLen,
                cameraForward.x,
                cameraForward.y,
                cameraForward.z,
                matrixForwardLen);
            return data;
        }

        // Convert direction vector to pitch/yaw Euler angles (radians).
        // forward=(fx, fy, fz), same yaw convention as GetViewMatricesSnapshot().get_rotation().
        const Vector3 localEuler = OW::DirectionToAimEuler(viewDir);
        const float localPitch = localEuler.X;
        const float localYaw = localEuler.Y;
        data.local_angle = Vector3(localPitch, localYaw, 0.0f);

        uint8_t rawData[32]{};
        if (OW::Config::aimVerboseLog &&
            SDK->read_range(viewDirAddress, rawData, sizeof(rawData))) {
            char hexBuf[96]{};
            int off = 0;
            for (int i = 0; i < 32 && off < static_cast<int>(sizeof(hexBuf)) - 4; i++) {
                off += std::snprintf(hexBuf + off, sizeof(hexBuf) - off, "%02X ", rawData[i]);
            }
            Diagnostics::Aim("angle.raw playerControllerBase=0x%llX viewDirAddress=0x%llX hex_32=%s",
                static_cast<unsigned long long>(playerControllerBase),
                static_cast<unsigned long long>(viewDirAddress),
                hexBuf);
        }

        Diagnostics::Aim("angle.viewdir source=%s playerControllerBase=0x%llX viewDirAddress=0x%llX fx=%.6f fy=%.6f fz=%.6f len=%.4f mem_valid=%d matrix_forward=(%.6f,%.6f,%.6f) matrix_len=%.4f",
            viewDirSource,
            static_cast<unsigned long long>(playerControllerBase),
            static_cast<unsigned long long>(viewDirAddress),
            viewDir.X,
            viewDir.Y,
            viewDir.Z,
            dirLen,
            memoryViewDirValid ? 1 : 0,
            cameraForward.x,
            cameraForward.y,
            cameraForward.z,
            matrixForwardLen);
        Diagnostics::Aim("angle.local_converted pitch_rad=%.6f yaw_rad=%.6f pitch_deg=%.2f yaw_deg=%.2f dir_len=%.4f",
            localPitch,
            localYaw,
            RAD2DEG(localPitch),
            RAD2DEG(localYaw),
            dirLen);

        // Guard: camera not yet resolved (identity or zero translation).
        // Computing angles from a zero camera produces bogus aim direction.
        {
            const float camLen = sqrtf(cameraPos.x * cameraPos.x +
                                       cameraPos.y * cameraPos.y +
                                       cameraPos.z * cameraPos.z);
            if (camLen < 1.0f) {
                Diagnostics::Aim("angle.early_return reason=camera_position_zero_or_near_origin cam=(%.3f,%.3f,%.3f) len=%.3f",
                    cameraPos.x, cameraPos.y, cameraPos.z, camLen);
                return data;
            }
        }
        float dx = world_target.X - cameraPos.x;
        float dy = world_target.Y - cameraPos.y;
        float dz = world_target.Z - cameraPos.z;
        float horizontalDist = sqrtf(dx*dx + dz*dz);
        float distance = sqrtf(dx*dx + dy*dy + dz*dz);
        float pitch = -atan2f(dy, horizontalDist);
        float yaw = atan2f(dx, dz);
        data.target_angle = Vector3(pitch, yaw, 0.0f);

        // Yaw wrapping: crosshair delta must take the shortest arc across  PI.
        {
            float yawDelta = data.target_angle.Y - data.local_angle.Y;
            const float rawYawDelta = yawDelta;
            while (yawDelta >  M_PI_F) yawDelta -= 2.0f * M_PI_F;
            while (yawDelta < -M_PI_F) yawDelta += 2.0f * M_PI_F;
            data.target_angle.Y = data.local_angle.Y + yawDelta;
            Diagnostics::Aim("angle.wrap raw_yaw_delta=%.9f wrapped_yaw_delta=%.9f local_yaw=%.9f raw_target_yaw=%.9f wrapped_target_yaw=%.9f",
                rawYawDelta,
                yawDelta,
                data.local_angle.Y,
                yaw,
                data.target_angle.Y);
        }

        Diagnostics::Aim("angle.compute world_target=(%.9f,%.9f,%.9f) camera=(%.9f,%.9f,%.9f) direction=(%.9f,%.9f,%.9f) distance=%.9f horizontal=%.9f camera_forward=(%.9f,%.9f,%.9f) local_angle=(%.9f,%.9f,%.9f) target_angle_rad=(%.9f,%.9f,%.9f) local_angle_deg=(%.6f,%.6f,%.6f) target_angle_deg=(%.6f,%.6f,%.6f)",
            world_target.X,
            world_target.Y,
            world_target.Z,
            cameraPos.x,
            cameraPos.y,
            cameraPos.z,
            dx,
            dy,
            dz,
            distance,
            horizontalDist,
            cameraForward.x,
            cameraForward.y,
            cameraForward.z,
            data.local_angle.X,
            data.local_angle.Y,
            data.local_angle.Z,
            data.target_angle.X,
            data.target_angle.Y,
            data.target_angle.Z,
            RAD2DEG(data.local_angle.X),
            RAD2DEG(data.local_angle.Y),
            RAD2DEG(data.local_angle.Z),
            RAD2DEG(data.target_angle.X),
            RAD2DEG(data.target_angle.Y),
            RAD2DEG(data.target_angle.Z));
        Diagnostics::Aim("angle.coordinate_check localAngleLen=%.9f targetAngleLen=%.9f cameraForwardLen=%.9f expected=local_and_target_are_euler_radians camera_forward_is_direction_vector",
            data.local_angle.Size(),
            data.target_angle.Size(),
            sqrtf(cameraForward.x * cameraForward.x + cameraForward.y * cameraForward.y + cameraForward.z * cameraForward.z));
        if (!std::isfinite(distance) || distance <= 0.0001f ||
            !std::isfinite(data.local_angle.X) ||
            !std::isfinite(data.local_angle.Y) ||
            !std::isfinite(data.target_angle.X) ||
            !std::isfinite(data.target_angle.Y)) {
            Diagnostics::Aim("angle.compute warning possible_coordinate_or_read_issue distance=%.9f local_angle=(%.9f,%.9f,%.9f) target_angle=(%.9f,%.9f,%.9f)",
                distance,
                data.local_angle.X,
                data.local_angle.Y,
                data.local_angle.Z,
                data.target_angle.X,
                data.target_angle.Y,
                data.target_angle.Z);
        }
        const int behavior = OW::Config::ClampAimBehaviorIndex(OW::Config::View_aimBehavior());
        if (!calculateSmoothing) {
            data.local_pos = CameraPosition();
            data.valid = std::isfinite(distance) && distance > 0.0001f &&
                std::isfinite(data.local_angle.X) && std::isfinite(data.local_angle.Y) &&
                std::isfinite(data.target_angle.X) && std::isfinite(data.target_angle.Y);
            return data;
        }
        const int dispatchMethod = methodOverride >= 0
            ? OW::Config::ClampAimMethodIndex(methodOverride)
            : OW::Config::AimBehaviorMethod(behavior);
        const float dispatchAcceleration = dispatchMethod == 4
            ? OW::Config::RuntimeAimMethodAcceleration(dispatchMethod,inheritTargetMotion)
            : (accelerated ? acceleration : 0.0f);
        data.smoothed_angle = OW::SmoothDispatchWithMethod(
            data.local_angle,
            data.target_angle,
            smooth,
            dispatchAcceleration,
            dispatchMethod,
            bezierSpeedOverride,
            commitSmoothingOutput,
            -1.0f,
            inheritTargetMotion
        );
        const Vector3 rawDelta = data.target_angle - data.local_angle;
        const Vector3 smoothDelta = data.smoothed_angle - data.local_angle;
        Diagnostics::Aim("angle.smooth raw_delta=(%.9f,%.9f,%.9f) raw_delta_len=%.9f smoothed_angle=(%.9f,%.9f,%.9f) smooth_delta=(%.9f,%.9f,%.9f) smooth_delta_len=%.9f smooth_input=%.9f accelerated=%d acceleration=%.9f",
            rawDelta.X,
            rawDelta.Y,
            rawDelta.Z,
            rawDelta.Size(),
            data.smoothed_angle.X,
            data.smoothed_angle.Y,
            data.smoothed_angle.Z,
            smoothDelta.X,
            smoothDelta.Y,
            smoothDelta.Z,
            smoothDelta.Size(),
            smooth,
            accelerated ? 1 : 0,
            dispatchAcceleration);
        data.local_pos = CameraPosition();
        data.valid = std::isfinite(distance) && distance > 0.0001f &&
            std::isfinite(data.smoothed_angle.X) && std::isfinite(data.smoothed_angle.Y);
        return data;
    }

    inline float AimNoise(float divisor) {
        const float direction = (rand() % 10 > 5) ? 1.f : -1.f;
        return direction * static_cast<float>(rand()) / RAND_MAX / divisor;
    }

    inline void ApplyAiAimNoise(Vector3& target, float divisor, bool clampSecondaryFov) {
        if (!OW::Config::aiaim) return;
        target.X += AimNoise(divisor);
        target.Y += AimNoise(divisor);
        target.Z += AimNoise(divisor);

        if (OW::Config::View_minFov1() > OW::Config::kMaxFovDeg) OW::Config::View_minFov1() = OW::Config::kMaxFovDeg;
        if (OW::Config::View_Fov() > OW::Config::kMaxFovDeg) OW::Config::View_Fov() = OW::Config::kMaxFovDeg;
        if (clampSecondaryFov) {
            if (OW::Config::View_minFov2() > OW::Config::kMaxFovDeg) OW::Config::View_minFov2() = OW::Config::kMaxFovDeg;
            if (OW::Config::View_Fov2() > OW::Config::kMaxFovDeg) OW::Config::View_Fov2() = OW::Config::kMaxFovDeg;
        }
    }

    inline bool TargetDelayReady(RuntimeState* state, bool stampHitDelay, bool resetWhenDisabled) {
        if (!OW::Config::targetdelay) {
            if (resetWhenDisabled && OW::Config::doingdelay) OW::Config::doingdelay = false;
            return true;
        }

        if (OW::Config::lastenemy != OW::Config::Targetenemyi) {
            Diagnostics::Aim("target_delay retarget last=%d current=%d targetdelaytime=%d",
                OW::Config::lastenemy,
                OW::Config::Targetenemyi,
                OW::Config::targetdelaytime);
            OW::Config::doingdelay = true;
        }
        if (!OW::Config::doingdelay) return true;

        OW::Config::lastenemy = OW::Config::Targetenemyi;
        if (OW::Config::timebeforedelay == 0) {
            OW::Config::timebeforedelay = GetTickCount();
            Diagnostics::Aim("target_delay early_return reason=started_wait target=%d waitMs=%d",
                OW::Config::Targetenemyi,
                OW::Config::targetdelaytime);
            return false;
        }

        const DWORD elapsed = GetTickCount() - OW::Config::timebeforedelay;
        if (elapsed < static_cast<DWORD>(OW::Config::targetdelaytime)) {
            Diagnostics::Aim("target_delay early_return reason=waiting target=%d elapsedMs=%lu waitMs=%d",
                OW::Config::Targetenemyi,
                static_cast<unsigned long>(elapsed),
                OW::Config::targetdelaytime);
            return false;
        }

        OW::Config::timebeforedelay = 0;
        OW::Config::doingdelay = false;
        if (stampHitDelay && state) state->hitbotdelaytime = GetTickCount();
        Diagnostics::Aim("target_delay ready target=%d elapsedMs=%lu",
            OW::Config::Targetenemyi,
            static_cast<unsigned long>(elapsed));
        return true;
    }

    inline void ArmDelayedShot(RuntimeState& state) {
        if (!OW::Config::hitboxdelayshoot) return;
        if (OW::Config::shooted || !IsAimKeyPressed()) {
            state.dodelay = true;
            state.hitbotdelaytime = 0;
        }
    }

    inline void PrimeDelayedShot(RuntimeState& state) {
        if (state.dodelay && !OW::Config::doingdelay) {
            state.hitbotdelaytime = GetTickCount();
            state.dodelay = false;
        }
    }

    inline bool DelayedShotTimedOut(RuntimeState& state) {
        if (!OW::Config::hitboxdelayshoot || state.hitbotdelaytime == 0) return false;
        state.afterdelaytime = GetTickCount();
        return state.afterdelaytime - state.hitbotdelaytime > OW::Config::hiboxdelaytime &&
               !OW::Config::doingdelay;
    }

    inline DWORD ResolveCurrentWeaponCadenceIntervalMs() {
        const WeaponCadenceTable& cadenceTable = RuntimeWeaponCadenceTable();
        static std::once_flag cadenceStatusLog;
        std::call_once(cadenceStatusLog, [&cadenceTable] {
            if (cadenceTable.Loaded()) {
                Diagnostics::Info(
                    "Weapon cadence TSV loaded. path=%s sourceRows=%zu runtimeRows=%zu discreteIntervals=%zu",
                    cadenceTable.LoadedPath().string().c_str(),
                    cadenceTable.SourceRowCount(),
                    cadenceTable.Size(),
                    cadenceTable.DiscreteIntervalCount());
            } else {
                Diagnostics::Warn(
                    "Weapon cadence TSV unavailable; runtime will use existing fallbacks. error=%s",
                    cadenceTable.LastError().c_str());
            }
        });

        const c_entity local = LocalEntity();
        const WeaponSpec* weapon = ResolveWeaponSpec(local.HeroID, OW::Config::View_aimbotAttack());
        if (!weapon)
            return 0;

        const WeaponCadenceEntry* cadence = cadenceTable.Find(weapon->weaponId);
        if (!cadence || !cadence->triggerCycleIntervalMs)
            return 0;

        return static_cast<DWORD>(std::lround(std::clamp(
            *cadence->triggerCycleIntervalMs,
            1.0f,
            60000.0f)));
    }

    inline DWORD DefaultMagneticShotIntervalMs() {
        const DWORD weaponCadenceMs = ResolveCurrentWeaponCadenceIntervalMs();
        if (weaponCadenceMs > 0)
            return weaponCadenceMs;

        const c_entity local = LocalEntity();
        const WeaponSpec* weapon = ResolveWeaponSpec(local.HeroID, OW::Config::View_aimbotAttack());
        if (!weapon)
            return 250;

        switch (weapon->aimClass) {
        case AimClass::HitscanAuto:
        case AimClass::ProjectileAuto:
        case AimClass::Beam:
            return 100;
        case AimClass::HitscanBurst:
            return 450;
        case AimClass::ProjectileSingle:
            return 350;
        case AimClass::ProjectileExplosive:
            return 500;
        case AimClass::Shotgun:
            return 650;
        case AimClass::HitscanSingle:
        default:
            return 250;
        }
    }

    inline DWORD ResolveFlickRestartDelayMs() {
        const DWORD shotClampMs = static_cast<DWORD>(OW::Config::ClampFlickShotClampMs(
            OW::Config::View_aimbotFlickShotClampMs()));
        const DWORD postFireDelayMs = static_cast<DWORD>(OW::Config::ClampFlickPostFireDelayMs(
            OW::Config::View_aimbotFlickPostFireDelayMs()));
        DWORD restartDelayMs = (std::max)(shotClampMs, postFireDelayMs);
        if (OW::Config::IsMagneticTriggerBehavior(OW::Config::View_aimBehavior())) {
            const float configured = OW::Config::ClampMagneticShotIntervalMs(
                OW::Config::View_aimbotMagneticShotIntervalMs());
            const DWORD magneticIntervalMs = configured > 0.0f
                ? static_cast<DWORD>(configured)
                : DefaultMagneticShotIntervalMs();
            restartDelayMs = (std::max)(restartDelayMs, magneticIntervalMs);
        }
        return restartDelayMs;
    }

    inline AimFirePhase::Tuning MagneticFirePhaseTuning() {
        AimFirePhase::Tuning tuning{};
        tuning.shotIntervalMs = static_cast<float>(ResolveFlickRestartDelayMs());
        tuning.pauseMs = (std::max)(
            OW::Config::ClampMagneticTimingMs(OW::Config::View_aimbotMagneticPostFirePauseMs(), 35.0f),
            OW::Config::ClampFlickPostFireDelayMs(OW::Config::View_aimbotFlickPostFireDelayMs()));
        tuning.recoveryMs = OW::Config::ClampMagneticTimingMs(
            OW::Config::View_aimbotMagneticRecoveryMs(), 90.0f);
        tuning.pauseYawScale = OW::Config::ClampMagneticAxisScale(
            OW::Config::View_aimbotMagneticPostFireYawScale(), 0.35f);
        tuning.pausePitchScale = OW::Config::ClampMagneticAxisScale(
            OW::Config::View_aimbotMagneticPostFirePitchScale(), 0.0f);
        tuning.preFireBoostWindowMs = OW::Config::ClampMagneticTimingMs(
            OW::Config::View_aimbotMagneticPreFireBoostWindowMs(), 45.0f);
        tuning.preFireBoostScale = OW::Config::ClampMagneticBoostScale(
            OW::Config::View_aimbotMagneticPreFireBoostScale());
        return tuning;
    }

    inline AimFirePhase::Phase ResolveMagneticFirePhase(const RuntimeState& state) {
        const bool shotLatched = OW::Config::shooted && state.lastFlickFireTick != 0;
        const float elapsedMs = shotLatched
            ? static_cast<float>(GetTickCount() - state.lastFlickFireTick)
            : 0.0f;
        return AimFirePhase::Resolve(elapsedMs, shotLatched, MagneticFirePhaseTuning());
    }

    inline void StampFlickFire(RuntimeState& state, DWORD submittedAt = GetTickCount()) {
        state.lastFlickFireTick = submittedAt;
        state.trajectoryWaitEntityKey = 0;
        state.trajectoryWaitStartedTick = 0;
    }

    inline void UpdateFlickShotCooldown(RuntimeState& state) {
        if (!OW::Config::shooted || !OW::Config::View_aimbotKeepFiring())
            return;
        if (state.lastFlickFireTick == 0) {
            OW::Config::shooted = false;
            return;
        }

        const DWORD elapsed = GetTickCount() - state.lastFlickFireTick;
        if (elapsed >= ResolveFlickRestartDelayMs()) {
            OW::Config::shooted = false;
            OW::Config::lasttime = 0;
        }
    }

    inline bool FlickPostFireLockoutActive(const RuntimeState& state) {
        const DWORD delayMs = static_cast<DWORD>(OW::Config::ClampFlickPostFireDelayMs(
            OW::Config::View_aimbotFlickPostFireDelayMs()));
        if (delayMs == 0 || state.lastFlickFireTick == 0)
            return false;
        return GetTickCount() - state.lastFlickFireTick < delayMs;
    }

    inline float TrackingDeadzoneDampingScale(const Vector3& aimTarget, float* outDistancePixels = nullptr) {
        const float radius = OW::Config::ClampTrackingDeadzonePixels(OW::Config::View_aimbotTrackingDeadzone());
        if (radius <= 0.0f) {
            if (outDistancePixels)
                *outDistancePixels = 0.0f;
            return 1.0f;
        }

        Vector2 screen{};
        if (!ProjectAimPoint(aimTarget, screen)) {
            if (outDistancePixels)
                *outDistancePixels = 0.0f;
            return 1.0f;
        }

        const float distance = OW::TargetingDetail::CrosshairCenter().Distance(screen);
        if (outDistancePixels)
            *outDistancePixels = distance;
        return OW::Config::TrackingDeadzoneDampingScale(distance, radius);
    }

    inline bool FlickTrajectoryWaitReady(RuntimeState& state, const c_entity& target) {
        if (!OW::Config::View_aimbotFlickTrajectoryWait())
            return true;

        Vector2 screen{};
        ProjectAimPoint(!IsZeroVector(target.head_pos) ? target.head_pos : target.chest_pos, screen);
        const OW::EntityMotionState motion = OW::TargetingDetail::EstimateMotionState(target, screen);
        const uint64_t entityKey = TwoStageEntityKey(target);
        const DWORD now = GetTickCount();
        if (state.trajectoryWaitEntityKey != entityKey) {
            state.trajectoryWaitEntityKey = entityKey;
            state.trajectoryWaitStartedTick = now;
        }

        const DWORD maxWaitMs = static_cast<DWORD>(OW::Config::ClampTrajectoryWaitMs(
            OW::Config::View_aimbotFlickTrajectoryWaitMs()));
        if (maxWaitMs == 0 || now - state.trajectoryWaitStartedTick >= maxWaitMs)
            return true;

        if (motion.kind != OW::EntityMotionState::Kind::AirborneRising)
            return true;

        constexpr float kGravityMetersPerSecond = 9.8f;
        const float verticalSpeed = (std::max)(0.0f, motion.verticalVelocity);
        const float timeToApexMs = (verticalSpeed / kGravityMetersPerSecond) * 1000.0f;
        const float apexWindowMs = OW::Config::ClampTrajectoryApexWindowMs(
            OW::Config::View_aimbotFlickTrajectoryApexWindowMs());
        if (!std::isfinite(timeToApexMs) || timeToApexMs <= apexWindowMs)
            return true;

        Diagnostics::Aim("flick trajectory_wait target=0x%llX vertical=%.3f timeToApexMs=%.3f windowMs=%.3f elapsedMs=%lu maxWaitMs=%lu",
            static_cast<unsigned long long>(entityKey),
            motion.verticalVelocity,
            timeToApexMs,
            apexWindowMs,
            static_cast<unsigned long>(now - state.trajectoryWaitStartedTick),
            static_cast<unsigned long>(maxWaitMs));
        return false;
    }

    inline bool FireHanzo(
        OW::OutputOwnerSource ownerSource = OW::OutputOwnerSource::GlobalAim,
        const char* ownerKey = kGlobalAimGeneratedFireOwnerKey,
        bool requireNewPress = false, DWORD* submittedAt = nullptr);

    inline FlickRepeat::SessionIdentity CurrentOrdinaryFlickIdentity();

    inline bool FirePrimaryNormal(
        OW::OutputOwnerSource ownerSource = OW::OutputOwnerSource::GlobalAim,
        const char* ownerKey = kGlobalAimGeneratedFireOwnerKey,
        bool requireNewPress = false, DWORD* submittedAt = nullptr,
        const FlickRepeat::SessionIdentity* expectedSession = nullptr) {
        if(expectedSession && CurrentOrdinaryFlickIdentity()!=*expectedSession) return false;
        return SubmitConfiguredAimShot(ownerSource,ownerKey,requireNewPress,submittedAt);
    }

    inline bool CompleteReleasedAimAtCrosshair(RuntimeState& state,bool noTarget) {
        if(!state.releasePending) return false;
        const auto* tuning=OW::Config::ActiveSlotBehaviorTuning();
        if(!tuning || (!noTarget && GetTickCount()-state.releaseActionStarted<
            static_cast<DWORD>(tuning->releaseAimTimeoutMs))) return false;
        DWORD submittedAt=0;
        if(SubmitConfiguredAimShot(OW::OutputOwnerSource::GlobalAim,kGlobalAimGeneratedFireOwnerKey,
                                   false,&submittedAt,true)) {
            StampFlickFire(state,submittedAt);OW::Config::shooted=true;++g_flickFires;
            Diagnostics::Info("[V2 shot] release.crosshair reason=%s",noTarget?"no_target":"aim_limit");
        }
        // A rejected submission retains the owned press for cleanup/retry and records no shot.
        return true;
    }

    inline void ReportReleasedAimProgress(RuntimeState& state,const AimData& aim,
                                          const Vector3& target,float radius,bool hit) {
        const DWORD now=GetTickCount();
        if(!state.releasePending || (state.releaseAimReportTick && now-state.releaseAimReportTick<100)) return;
        state.releaseAimReportTick=now;
        const float distance=aim.local_pos.DistTo(target);
        Diagnostics::Info("[V2 shot] release.aim elapsedMs=%lu hit=%d errorDeg=%.4f windowDeg=%.4f charge=%.2f",
            static_cast<unsigned long>(now-state.releaseActionStarted),hit?1:0,
            RAD2DEG((aim.target_angle-aim.local_angle).Size()),distance>0?RAD2DEG(radius/distance):0,
            OW::ReadWeaponChargePercent(LocalEntity(),OW::Config::View_aimbotAttack()));
    }

    inline bool FireHanzo(
        OW::OutputOwnerSource ownerSource,
        const char* ownerKey,
        bool requireNewPress, DWORD* submittedAt) {
        if (!OW::RuntimeWorkersRunning() || !OW::HasFreshViewMatrices())
            return false;
        if (!GeneratedFireAllowedForSource(ownerSource))
            return false;
        const std::uint64_t operationGeneration =
            V2::Output::ActiveRuntimeSnapshot().generation;
        const c_entity local = LocalEntity();
        if (local.skill2act) {
            Diagnostics::Aim("hanzo.fire mode=storm_arrow_tap");
            return ClickMouseButton(
                0,
                10,
                ownerSource,
                ownerKey,
                requireNewPress, submittedAt);
        }

        Diagnostics::Aim("hanzo.fire mode=charge_release_no_physical_mask keyMask=0x1");
        const bool accepted = ReleaseDmaMouseKey(
            0x1, 10, false, ownerSource, ownerKey, operationGeneration, submittedAt);
        RecoverHanzoPostFireMouseState(
            ownerSource,
            ownerKey,
            operationGeneration);
        return accepted;
    }

    inline void RunAutoScaleFov() {
        if (!OW::Config::View_autoscalefov()) return;

        auto fvec = OW::GetVector3forfov();
        c_entity fov_target{};
        if (IsZeroVector(fvec) || !OW::TryGetTargetEntity(OW::Config::Targetenemyifov, fov_target)) {
            OW::Config::View_Fov() = OW::Config::View_minFov1();
            OW::Config::View_Fov2() = OW::Config::View_minFov2();
            return;
        }

        const Vector3 camera = CameraPosition();
        const float bodyAngleDeg = OW::TargetingDetail::AngularSeparationDegFromCamera(
            camera,
            fov_target.head_pos,
            fov_target.chest_pos);
        if (std::isfinite(bodyAngleDeg) && bodyAngleDeg > 0.0f) {
            const float scaledFovDeg = OW::Config::ClampFovDeg(bodyAngleDeg * 4.0f);
            OW::Config::View_Fov() = (std::max)(scaledFovDeg, OW::Config::View_minFov1());
            OW::Config::View_Fov2() = (std::max)(scaledFovDeg, OW::Config::View_minFov2());
        } else {
            OW::Config::View_Fov() = OW::Config::View_minFov1();
            OW::Config::View_Fov2() = OW::Config::View_minFov2();
        }
    }

    inline void RunCloseRangeActions(
        const Vector3& target_pos,
        OW::OutputOwnerSource ownerSource) {
        if (!OW::RuntimeWorkersRunning() || !OW::HasFreshViewMatrices())
            return;
        if (ownerSource == OW::OutputOwnerSource::GlobalAim &&
            OW::Config::View_aimbotRequireActionHeld()) {
            return;
        }
        const float dist = CameraPosition().DistTo(target_pos);
        if (OW::Config::health <= OW::Config::meleehealth &&
            dist <= OW::Config::meleedistance &&
            OW::Config::AutoMelee) {
            OW::PulseAction(OW::GameAction::Melee);
        }
        if (OW::Config::health <= OW::Config::AutoRMBhealth &&
            dist <= OW::Config::AutoRMBdistance &&
            OW::Config::AutoRMB) {
            ClickMouseButton(
                1,
                10,
                ownerSource,
                ownerSource == OW::OutputOwnerSource::Trigger
                    ? kTriggerCloseRangeOwnerKey
                    : kGlobalAimCloseRangeOwnerKey);
        }
    }

    inline bool IsTriggerKeyPressed(int keySetting) {
        return IsConfiguredAimKeyPressed(keySetting);
    }

    inline bool ResolveCurrentAimSlotPredictionEnabled() {
        const c_entity local = OW::TargetingDetail::SnapshotLocalEntity();
        const WeaponSpec* weapon = OW::ResolveWeaponSpec(local.HeroID, OW::Config::View_aimbotAttack());
        return OW::ResolvePredictionEnabled(
            OW::ClampPredictionOverride(OW::Config::View_aimbotPredictionMode()),
            weapon,
            OW::Config::View_Prediction());
    }

    inline bool InputSequenceBlocksAim(const char* caller,
                                       ExecutionSource requester = ExecutionSource::GlobalAim) {
        if (!OW::ShouldBlockForActiveSequence(requester))
            return false;

        static DWORD lastLogTick = 0;
        const DWORD now = GetTickCount();
        if (lastLogTick == 0 || now - lastLogTick >= 500) {
            const bool heldTriggerReservation =
                requester == ExecutionSource::Trigger &&
                OW::AnyInputSequenceActivationHeld() &&
                !OW::AnyInputSequenceActive();
            Diagnostics::Aim("aim_trigger blocked reason=%s caller=%s",
                heldTriggerReservation ? "sequence_hotkey_held" : "input_sequence_active",
                caller ? caller : "unknown");
            lastLogTick = now;
        }
        return true;
    }

    inline bool TryReadTriggerReloadingState(std::uint64_t skillBase,
                                             bool& reloading) {
        constexpr uint16_t kReloadStateSkill = 0x4BF;
        if (!SDK || !skillBase)
            return false;

        __try {
            reloading = OW::IsSkillActivate1(
                skillBase + 0x40,
                0,
                kReloadStateSkill);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    inline bool RefreshTriggerReloadingState() {
        constexpr DWORD kReloadPollIntervalMs = 50;
        static DWORD lastReadTick = 0;
        static std::uint64_t lastConnectionEpoch = 0;
        static std::uint64_t lastSkillBase = 0;
        static bool cachedReloading = false;

        const c_entity local = LocalEntity();
        const std::uint64_t connectionEpoch = OW::ProcessConnection::ConnectionEpoch();
        if (!SDK || !local.SkillBase || connectionEpoch == 0 ||
            !OW::ProcessConnection::IsConnected()) {
            cachedReloading = OW::Config::reloading;
            lastReadTick = 0;
            lastConnectionEpoch = connectionEpoch;
            lastSkillBase = local.SkillBase;
            return cachedReloading;
        }

        const DWORD now = GetTickCount();
        const bool identityChanged =
            lastConnectionEpoch != connectionEpoch || lastSkillBase != local.SkillBase;
        if (!identityChanged && lastReadTick != 0 &&
            now - lastReadTick < kReloadPollIntervalMs) {
            return cachedReloading;
        }

        bool reloading = OW::Config::reloading;
        const bool readSucceeded = TryReadTriggerReloadingState(
            local.SkillBase,
            reloading);
        if (!readSucceeded)
            reloading = OW::Config::reloading;

        lastReadTick = now;
        lastConnectionEpoch = connectionEpoch;
        lastSkillBase = local.SkillBase;
        cachedReloading = reloading;
        if (readSucceeded)
            OW::Config::reloading = reloading;
        return cachedReloading;
    }

    inline void RunTriggerbot(bool secondary, float origin_sens) {
        if (!OW::RuntimeWorkersRunning() || !OW::HasFreshViewMatrices())
            return;
        if (InputSequenceBlocksAim(secondary ? "triggerbot2" : "triggerbot", ExecutionSource::Trigger))
            return;

        const int mode = secondary ? OW::Config::triggerbotMode2 : OW::Config::View_triggerbotMode();
        const int keySetting = secondary ? OW::Config::triggerbotKey2 : OW::Config::View_triggerbotKey();
        const float shotInterval = secondary ? OW::Config::triggerbotShotInterval2 : OW::Config::View_triggerbotShotInterval();
        const bool chargeAware = secondary ? OW::Config::triggerbotChargeAware2 : OW::Config::View_triggerbotChargeAware();
        const float minCharge = secondary ? OW::Config::triggerbotMinCharge2 : OW::Config::View_triggerbotMinCharge();
        const bool disableWhileReloading = secondary
            ? OW::Config::triggerbotDisableWhileReloading2
            : OW::Config::View_triggerbotDisableWhileReloading();
        const bool ignoreInvisible = secondary ? OW::Config::triggerbotIgnoreInvisible2 : OW::Config::View_triggerbotIgnoreInvisible();
        const OW::TriggerBoneMask boneMask = secondary
            ? OW::Config::triggerbotBoneMask2
            : OW::Config::View_triggerbotBoneMask();
        bool& toggleActive = secondary ? OW::Config::triggerbotToggleActive2 : OW::Config::triggerbotToggleActive;
        DWORD& lastFireTick = secondary ? OW::Config::triggerbotLastFireTick2 : OW::Config::triggerbotLastFireTick;

        // 1. Determine armed state
        bool armed = false;
        switch (mode) {
        case 0: // Hold
            armed = IsTriggerKeyPressed(keySetting);
            break;
        case 1: { // Toggle
            const int bindingVk = OW::get_bind_id(keySetting);
            const KeyState::KeyStateVkSample sample =
                KeyState::SampleVk(bindingVk);
            static OW::SampledToggleState primaryToggleState{};
            static OW::SampledToggleState secondaryToggleState{};
            OW::SampledToggleState& toggleState =
                secondary ? secondaryToggleState : primaryToggleState;
            armed = OW::UpdateSampledToggle(
                toggleState, sample.available, sample.down, toggleActive,
                OW::ProcessConnection::ConnectionEpoch(), bindingVk);
            break;
        }
        case 2: // Always
            armed = true;
            break;
        default:
            break;
        }

        if (!armed) return;

        // 2. Optional reload gate. Refresh at a bounded rate because the
        // general local-skill cache rotates this state too slowly for reloads.
        if (OW::Config::ShouldBlockTriggerForReload(
                disableWhileReloading,
                RefreshTriggerReloadingState())) {
            static DWORD lastReloadBlockLogTickPrimary = 0;
            static DWORD lastReloadBlockLogTickSecondary = 0;
            DWORD& lastLogTick = secondary
                ? lastReloadBlockLogTickSecondary
                : lastReloadBlockLogTickPrimary;
            const DWORD now = GetTickCount();
            if (lastLogTick == 0 || now - lastLogTick >= 500) {
                Diagnostics::Aim(
                    "triggerbot blocked reason=reloading lane=%s",
                    secondary ? "secondary" : "primary");
                lastLogTick = now;
            }
            return;
        }

        // 3. Shot interval cooldown. A zero slot value means that the
        // experimental weapon cadence table owns the interval; an explicit
        // user value remains the highest-priority override.
        {
            const DWORD now = GetTickCount();
            const DWORD intervalMs = shotInterval > 0.0f
                ? static_cast<DWORD>(shotInterval * 5.0f) // 0-100 -> 0-500 ms
                : ResolveCurrentWeaponCadenceIntervalMs();
            if (intervalMs > 0 && lastFireTick != 0 && (now - lastFireTick) < intervalMs)
                return;
        }

        // 4. Find a target whose selected skeleton hitbox is under the crosshair.
        const bool predit = ResolveCurrentAimSlotPredictionEnabled();
        const auto frame=OW::consumerFrame?OW::consumerFrame:OW::SnapshotFrame();
        const TargetCandidate triggerTarget = OW::AcquireTriggerTarget(
            *frame, boneMask,
            predit,
            ignoreInvisible,
            secondary);
        if (!triggerTarget.valid)
            return;
        const c_entity target = triggerTarget.entitySnapshot;

        // 5. The direct skeleton intersection already passed; Flick2nd keeps
        // its additional stage gate on top of that hit.
        const bool triggerReady = (!secondary && OW::Config::IsFlick2ndBehavior(OW::Config::View_aimBehavior()))
            ? TwoStageTriggerOpenForTarget(target)
            : true;
        if (!triggerReady)
            return;

        if (InputSequenceBlocksAim(secondary ? "triggerbot2_fire" : "triggerbot_fire", ExecutionSource::Trigger))
            return;

        // 6. Charge awareness
        if (chargeAware) {
            c_entity local = LocalEntity();
            float charge = 100.0f;

            if (local.HeroID == OW::eHero::HERO_HANJO) {
                charge = readult(local.SkillBase + 0x40, 0xB, 0x2A5) * 100.0f;
            } else if (local.HeroID == OW::eHero::HERO_WIDOWMAKER) {
                charge = IsInputVkDown(VK_RBUTTON)
                    ? readult(local.SkillBase + 0x40, 0xB, 0x2567) * 100.0f
                    : 0.0f;
            }

            if (charge < minCharge) return;
        }

        // 7. Fire
        const c_entity local = LocalEntity();
        if (local.HeroID == OW::eHero::HERO_HANJO) {
            SetSensitivityLocked(true, origin_sens);
            FireHanzo(
                OW::OutputOwnerSource::Trigger,
                kTriggerGeneratedFireOwnerKey);
            SetSensitivityLocked(false, origin_sens);
        } else {
            const OW::WeaponSpec* weapon = OW::ResolveWeaponSpec(local.HeroID, OW::Config::View_aimbotAttack());
            PressWithSensitivity(
                OW::ResolveGeneratedFireKeyMask(
                    weapon,
                    OW::Config::View_aimbotAttack()),
                origin_sens,
                2,
                OW::OutputOwnerSource::Trigger,
                kTriggerGeneratedFireOwnerKey);
        }
        lastFireTick = GetTickCount();
    }

    inline bool ShouldYieldToSecondaryAim() {
        return OW::Config::highPriority && IsSecondAimKeyPressed();
    }

    inline bool AimSessionTimedOut(DWORD sessionStartedTick, const char* caller) {
        const DWORD timeoutMs = OW::TargetingDetail::ResolveAimSessionTimeoutMs();
        if (timeoutMs == 0)
            return false;
        const DWORD elapsed = GetTickCount() - sessionStartedTick;
        if (elapsed < timeoutMs)
            return false;

        Diagnostics::Aim("aim_session timeout caller=%s elapsedMs=%lu timeoutMs=%lu",
            caller ? caller : "unknown",
            static_cast<unsigned long>(elapsed),
            static_cast<unsigned long>(timeoutMs));
        OW::TargetingDetail::ResetTargetLockRuntime();
        return true;
    }

    inline void ObserveAimHoldWindows(RuntimeState& state) {
        const int selectedKey = OW::Config::View_aim_key();
        const bool timedBehavior = OW::Config::View_Tracking() ||
            (OW::Config::View_Flick() &&
             OW::Config::IsMagneticTriggerBehavior(OW::Config::View_aimBehavior()));
        const DWORD now = GetTickCount();
        const DWORD timeoutMs = OW::TargetingDetail::ResolveAimSessionTimeoutMs();
        for (size_t key = 0; key < state.aimHoldWindows.size(); ++key) {
            if (!state.aimHoldWindows[key].active &&
                !(timedBehavior && selectedKey == static_cast<int>(key)))
                continue;
            const auto sample = KeyState::SampleVk(OW::get_bind_id(static_cast<int>(key)));
            OW::AimHoldWindow::Observe(state.aimHoldWindows[key], sample.available,
                sample.down, now, timeoutMs, timedBehavior && selectedKey == static_cast<int>(key));
        }
    }

    inline bool TrackingHoldTimedOut(RuntimeState& state) {
        if(UsesReleaseKeyUpExecution()) return false; // this action has its own bounded release window
        const int key = OW::Config::View_aim_key();
        if (key < 0 || key >= static_cast<int>(state.aimHoldWindows.size()))
            return true;
        auto& hold = state.aimHoldWindows[static_cast<size_t>(key)];
        if (!hold.active)
            return true;
        const bool wasTimedOut = hold.timedOut;
        if (!OW::AimHoldWindow::Expired(hold, GetTickCount()))
            return false;
        if (!wasTimedOut) {
            Diagnostics::Aim("aim_hold timeout key=%d elapsedMs=%lu timeoutMs=%lu blockedUntilRelease=1",
                key, static_cast<unsigned long>(GetTickCount() - hold.startedAt),
                static_cast<unsigned long>(hold.timeoutMs));
        }
        OW::TargetingDetail::ResetTargetLockRuntime();
        return true;
    }

    inline void RunTracking(RuntimeState& state, float origin_sens) {
        const std::uint64_t connectionEpoch =
            OW::ProcessConnection::ConnectionEpoch();
        const std::uint64_t outputTransitionEpoch =
            OW::RuntimeOutputTransitionEpoch();
        if (connectionEpoch == 0 || !OW::ProcessConnection::IsConnected()) {
            if (state.trackingTargetKey != 0)
                ResetTrackingTargetContinuity(state, "connection_unavailable");
            return;
        }
        if (InputSequenceBlocksAim("tracking")) {
            if (state.trackingTargetKey != 0)
                ResetTrackingTargetContinuity(state, "sequence_preempted");
            return;
        }
        if (!IsAimKeyPressed()) {
            ResetTrackingSession(state);
            return;
        }
        if (OW::Config::reloading) {
            if (state.trackingTargetKey != 0)
                ResetTrackingTargetContinuity(state, "reloading");
            return;
        }
        const TrackingSessionIdentity sessionIdentity = CurrentTrackingSessionIdentity();
        EnsureTrackingSession(state, sessionIdentity);
        if (TrackingHoldTimedOut(state)) {
            static DWORD lastHeldTimeoutLogTick = 0;
            const DWORD now = GetTickCount();
            if (lastHeldTimeoutLogTick == 0 || now - lastHeldTimeoutLogTick >= 250) {
                Diagnostics::Aim("tracking skipped reason=session_timeout_held hero=0x%llX slot=%d key=%d",
                    static_cast<unsigned long long>(sessionIdentity.heroId),
                    sessionIdentity.slotIndex + 1,
                    sessionIdentity.aimKey);
                lastHeldTimeoutLogTick = now;
            }
            OW::TargetingDetail::ResetTargetLockRuntime();
            if (state.trackingTargetKey != 0)
                ResetTrackingTargetContinuity(state, "session_timeout_held");
            return;
        }
        g_trackingAttempts++;
        const int behavior = OW::Config::ClampAimBehaviorIndex(OW::Config::View_aimBehavior());
        const OW::AimStartLimiterProfile entryStartLimiter =
            OW::Config::ResolveAimStartLimiterProfile(behavior);

        Diagnostics::Aim("tracking.enter originSens=%.6f reloading=%d scale=%.6f baseSpeed=%.6f method=%d prediction=%d targetDelay=%d startLimiter=%d initialCapDegPerSec=%.3f capRiseDegPerSec2=%.3f restartOnTargetChange=%d",
            origin_sens,
            OW::Config::reloading ? 1 : 0,
            OW::Config::View_Tracking_smooth(),
            OW::Config::AimBehaviorBaseSpeed(behavior),
            OW::Config::AimBehaviorMethod(behavior),
            OW::Config::View_Prediction() ? 1 : 0,
            OW::Config::targetdelay ? 1 : 0,
            entryStartLimiter.enabled ? 1 : 0,
            entryStartLimiter.initialCapDegPerSec,
            entryStartLimiter.capRiseDegPerSec2,
            entryStartLimiter.restartOnTargetChange ? 1 : 0);

        // ---- Dry-run mode: log diagnostic info, don't move cursor ----
        if (OW::Config::aimDryRun) {
            static DWORD lastDryRunLog = 0;
            DWORD now = GetTickCount();
            if (now - lastDryRunLog >= (DWORD)OW::Config::aimDryRunLogIntervalMs) {
                lastDryRunLog = now;
                const Vector3 vec = OW::GetVector3(OW::Config::View_Prediction());
            OW::AimCandidateConfig selectedSettings1(OW::Config::selectedAimHero);
                if (!IsZeroVector(vec)) {
                    AimData aim = BuildAimData(
                        vec,
                        false,
                        OW::Config::AimBehaviorSmoothInput(behavior, OW::Config::View_Tracking_smooth()),
                        OW::Config::AimBehaviorAcceleration(behavior),
                        -1,
                        -1.0f,
                        false);
                    c_entity dryRunTarget{};
                    const std::uint64_t targetKey = IsPrimaryTargetActionable(dryRunTarget)
                        ? TwoStageEntityKey(dryRunTarget)
                        : 0;
                    OW::AimStartLimiterState previewState = state.trackingStartLimiter;
                    const OW::AimStartLimiterResult limiterPreview = OW::ApplyAimStartLimiter(
                        entryStartLimiter,
                        previewState,
                        state.trackingSessionGeneration,
                        connectionEpoch,
                        targetKey,
                        aim.local_angle,
                        aim.smoothed_angle,
                        OW::kAimStartLimiterDefaultDeltaTimeSeconds);
                    const float yawCountsPerRadian = OW::Config::OutputYawCountsPerRadian();
                    const float pitchCountsPerRadian = OW::Config::OutputPitchCountsPerRadian();
                    Diagnostics::Aim("dryrun.tracking local_angle_deg=(%.4f,%.4f) target_angle_deg=(%.4f,%.4f) delta_deg=(%.4f,%.4f) "
                        "delta_counts_est=(x_from_yaw=%.1f,y_from_pitch=%.1f) yawCountsPerRad=%.1f pitchCountsPerRad=%.1f would_move=%d target_pos=(%.1f,%.1f,%.1f)",
                        RAD2DEG(aim.local_angle.X), RAD2DEG(aim.local_angle.Y),
                        RAD2DEG(aim.target_angle.X), RAD2DEG(aim.target_angle.Y),
                        RAD2DEG(aim.target_angle.X - aim.local_angle.X), RAD2DEG(aim.target_angle.Y - aim.local_angle.Y),
                        -(aim.target_angle.Y - aim.local_angle.Y) * yawCountsPerRadian,
                        (aim.target_angle.X - aim.local_angle.X) * pitchCountsPerRadian,
                        yawCountsPerRadian,
                        pitchCountsPerRadian,
                        1, vec.X, vec.Y, vec.Z);
                    Diagnostics::Aim("dryrun.tracking start_limiter enabled=%d requestedDegPerSec=%.3f capDegPerSec=%.3f appliedDegPerSec=%.3f limited=%d reset=%s wouldMove=%d",
                        entryStartLimiter.enabled ? 1 : 0,
                        limiterPreview.requestedSpeedDegPerSec,
                        limiterPreview.capDegPerSec,
                        limiterPreview.appliedSpeedDegPerSec,
                        limiterPreview.limited ? 1 : 0,
                        OW::AimStartLimiterResetReasonName(limiterPreview.resetReason),
                        (limiterPreview.outputAngle - aim.local_angle).Size() > 0.000001f ? 1 : 0);
                } else {
                    Diagnostics::Aim("dryrun.tracking no_target_vector targetIndex=%d entities=%zu",
                        OW::Config::Targetenemyi,
                        OW::TargetingDetail::SnapshotEntities().size());
                }
            }
            OW::RuntimeWorkerSleep(1);
            return; // Don't actually move cursor
        }

        bool fireHeld = false;
        const OW::c_entity localForWeapon = LocalEntity();
        const OW::WeaponSpec* trackingWeapon = OW::ResolveWeaponSpec(localForWeapon.HeroID, OW::Config::View_aimbotAttack());
        const int holdFireButton = OW::ResolveTrackingHoldMouseButton(trackingWeapon, OW::Config::View_aimbotAttack());
        auto synchronizeHeldFireState = [&]() {
            if (fireHeld &&
                !OW::RuntimeOutputScheduler().IsActive(
                    kGlobalAimTrackingFireOwnerKey)) {
                fireHeld = false;
            }
        };
        auto releaseHeldFire = [&]() {
            synchronizeHeldFireState();
            if (fireHeld && holdFireButton >= 0) {
                const OW::ActionOutputStatus releaseStatus =
                    OW::SendMouseButtonActionState(
                    holdFireButton,
                    false,
                    OW::OutputOwnerSource::GlobalAim,
                    kGlobalAimTrackingFireOwnerKey);
                if (OW::ActionOutputSucceeded(releaseStatus) ||
                    !OW::RuntimeOutputScheduler().IsActive(
                        kGlobalAimTrackingFireOwnerKey)) {
                    fireHeld = false;
                }
            }
        };

        while (OW::RuntimeWorkersRunning() && IsAimKeyPressed() &&
               OW::ProcessConnection::IsConnected() &&
               OW::ProcessConnection::ConnectionEpoch() == connectionEpoch &&
               OW::RuntimeOutputTransitionMatches(outputTransitionEpoch) &&
               !OW::Config::reloading &&
               !OW::ShouldBlockForActiveSequence(ExecutionSource::GlobalAim)) {
            OW::consumerFrame=OW::SnapshotFrame();
            synchronizeHeldFireState();
            const OW::AimStartLimiterProfile startLimiterProfile =
                OW::Config::ResolveAimStartLimiterProfile(behavior);
            if (!startLimiterProfile.enabled && state.trackingStartLimiter.started)
                OW::ResetAimStartLimiter(state.trackingStartLimiter);
            if (startLimiterProfile.enabled &&
                state.trackingStartLimiter.started &&
                state.trackingStartLimiter.connectionEpoch != 0 &&
                state.trackingStartLimiter.connectionEpoch != connectionEpoch) {
                state.trackingStartLimiter.lastLoopAt = {};
            }
            const float startLimiterDeltaTime = startLimiterProfile.enabled
                ? OW::ObserveAimStartLimiterLoop(state.trackingStartLimiter)
                : OW::kAimStartLimiterDefaultDeltaTimeSeconds;
            if (TrackingHoldTimedOut(state)) {
                break;
            }
            bool holdThisTick = false;
            const OW::TargetCandidate candidate =
                OW::AcquireTarget(
                    *OW::consumerFrame,
                    OW::Config::View_Prediction(),
                    OW::Config::View_aimbotIgnoreInvisible(),
                    OW::TrackingContinuityPolicy::kTargetDropoutGraceMs);
            OW::AimCandidateConfig selectedSettings(candidate.valid?candidate.entitySnapshot.HeroID:0);
            const c_entity target = candidate.entitySnapshot;
            const Vector3 vec = candidate.valid
                ? TrackingSampleAgeCompensatedAimPoint(candidate, GetTickCount())
                : Vector3{};
            if (!candidate.valid || IsZeroVector(vec)) {
                HoldTrackingTargetDropout(state, GetTickCount());
                Diagnostics::Aim("tracking no_move reason=no_target_vector targetIndex=%d entities=%zu",
                    OW::Config::Targetenemyi,
                    OW::TargetingDetail::PublishedEntityCount());
                releaseHeldFire();
            } else if (!IsPrimaryTargetSnapshotActionable(target)) {
                HoldTrackingTargetDropout(state, GetTickCount());
                Diagnostics::Aim("tracking no_move reason=target_not_actionable targetIndex=%d vec=(%.9f,%.9f,%.9f)",
                    OW::Config::Targetenemyi,
                    vec.X,
                    vec.Y,
                    vec.Z);
                releaseHeldFire();
            } else {
                const uint64_t trackingTargetKey = candidate.entityKey != 0
                    ? candidate.entityKey
                    : OW::TargetingDetail::StableTargetKey(target);
                const uint64_t trackingTargetActorKey = TwoStageEntityKey(target);
                const uint64_t trackingControlTargetKey =
                    OW::TrackingContinuityPolicy::ControlTargetKey(
                        trackingTargetKey,
                        trackingTargetActorKey);
                PrepareTrackingTargetContinuity(
                    state,
                    trackingTargetKey,
                    trackingTargetActorKey);
                state.trackingTargetMissingSinceTick = 0;
                const Vector3 aimTarget = vec;
                float deadzoneDistance = 0.0f;
                const float deadzoneDampingScale = TrackingDeadzoneDampingScale(aimTarget, &deadzoneDistance);
                if (deadzoneDampingScale <= 0.0f) {
                    OW::HoldAimSmoothingState();
                    state.trackingInDeadzone = true;
                    holdThisTick = OW::RuntimeWorkersRunning() && OW::HasFreshViewMatrices() &&
                        ShouldHoldFireWhileTracking(holdFireButton);
                    if (holdThisTick && !fireHeld && holdFireButton >= 0) {
                        synchronizeHeldFireState();
                        fireHeld = OW::ActionOutputSucceeded(
                            OW::SendMouseButtonActionState(
                                holdFireButton,
                                true,
                                OW::OutputOwnerSource::GlobalAim,
                                kGlobalAimTrackingFireOwnerKey));
                    } else if (!holdThisTick) {
                        releaseHeldFire();
                    }
                    OW::RuntimeWorkerSleep(1);
                    RunAutoScaleFov();
                    if (ShouldYieldToSecondaryAim()) break;
                    continue;
                }
                if (state.trackingInDeadzone) {
                    OW::HoldAimSmoothingState();
                    state.trackingInDeadzone = false;
                }
                const float smoothInput = OW::Config::AimBehaviorSmoothInput(
                    behavior,
                    OW::Config::View_Tracking_smooth(),
                    deadzoneDampingScale);
                if (OW::Config::aimVerboseLog && deadzoneDampingScale < 1.0f) {
                    Diagnostics::Aim("tracking.deadzone_damping distancePx=%.3f radiusPx=%.3f scale=%.6f smoothInput=%.6f",
                        deadzoneDistance,
                        OW::Config::ClampTrackingDeadzonePixels(OW::Config::View_aimbotTrackingDeadzone()),
                        deadzoneDampingScale,
                        smoothInput);
                }
                AimData aim = BuildAimData(
                    aimTarget,
                    false,
                    smoothInput,
                    OW::Config::AimBehaviorAcceleration(behavior),
                    -1,
                    -1.0f,
                    !startLimiterProfile.enabled);
                ApplyAiAimNoise(aim.smoothed_angle, 500.f, true);
                holdThisTick = OW::RuntimeWorkersRunning() && OW::HasFreshViewMatrices() &&
                    ShouldHoldFireWhileTracking(holdFireButton);
                if (holdThisTick && !fireHeld && holdFireButton >= 0) {
                    synchronizeHeldFireState();
                    fireHeld = OW::ActionOutputSucceeded(
                        OW::SendMouseButtonActionState(
                            holdFireButton,
                            true,
                            OW::OutputOwnerSource::GlobalAim,
                            kGlobalAimTrackingFireOwnerKey));
                }

                if (!IsZeroVector(aim.smoothed_angle)) {
                    if (!TargetDelayReady(nullptr, false, false)) continue;
                    Vector3 outputAngle = aim.smoothed_angle;
                    OW::AimStartLimiterResult limiterResult{};
                    if (startLimiterProfile.enabled) {
                        limiterResult = OW::ApplyAimStartLimiter(
                            startLimiterProfile,
                            state.trackingStartLimiter,
                            state.trackingSessionGeneration,
                            connectionEpoch,
                            trackingControlTargetKey,
                            aim.local_angle,
                            aim.smoothed_angle,
                            startLimiterDeltaTime);
                        outputAngle = limiterResult.outputAngle;
                        OW::CommitAimSmoothingOutput(outputAngle - aim.local_angle);
                        if (OW::Config::aimVerboseLog) {
                            Diagnostics::Aim("tracking.start_limiter requestedDegPerSec=%.3f capDegPerSec=%.3f appliedDegPerSec=%.3f limited=%d reset=%s targetKey=0x%llX session=%llu connectionEpoch=%llu dt=%.6f",
                                limiterResult.requestedSpeedDegPerSec,
                                limiterResult.capDegPerSec,
                                limiterResult.appliedSpeedDegPerSec,
                                limiterResult.limited ? 1 : 0,
                                OW::AimStartLimiterResetReasonName(limiterResult.resetReason),
                                static_cast<unsigned long long>(trackingControlTargetKey),
                                static_cast<unsigned long long>(state.trackingSessionGeneration),
                                static_cast<unsigned long long>(connectionEpoch),
                                startLimiterDeltaTime);
                        }
                    }

                    const Vector3 outputDelta = outputAngle - aim.local_angle;
                    const bool moved = !startLimiterProfile.enabled ||
                        outputDelta.Size() > 0.000001f;
                    if (moved) {
                        MoveAimDelta(aim.local_angle, outputAngle);
                        g_trackingMoves++;
                    }
                    if (OW::Config::aimVerboseLog) {
                        Diagnostics::Aim("tracking.tick moved=%d delta_counts_est=(x_from_yaw=%.1f,y_from_pitch=%.1f) target_dist=%.1f",
                            moved ? 1 : 0,
                            -outputDelta.Y * OW::Config::OutputYawCountsPerRadian(),
                            outputDelta.X * OW::Config::OutputPitchCountsPerRadian(),
                            CameraPosition().DistTo(aimTarget));
                    }
                    if (moved) {
                        RunCloseRangeActions(
                            aimTarget,
                            OW::OutputOwnerSource::GlobalAim);
                    }
                } else {
                    Diagnostics::Aim("tracking no_move reason=smoothed_angle_zero local=(%.9f,%.9f,%.9f) target=(%.9f,%.9f,%.9f)",
                        aim.local_angle.X,
                        aim.local_angle.Y,
                        aim.local_angle.Z,
                        aim.target_angle.X,
                        aim.target_angle.Y,
                        aim.target_angle.Z);
                }

                if (LocalEntity().PlayerHealth < OW::Config::SkillHealth) break;
            }

            if (!holdThisTick)
                releaseHeldFire();

            OW::RuntimeWorkerSleep(1);
            RunAutoScaleFov();
            if (ShouldYieldToSecondaryAim()) break;
        }
        releaseHeldFire();
        if (state.trackingTargetKey != 0)
            ResetTrackingTargetContinuity(state, "tracking_loop_exit");
    }

    inline void RunMagneticTrigger(RuntimeState& state, float origin_sens) {
        const std::uint64_t connectionEpoch =
            OW::ProcessConnection::ConnectionEpoch();
        const std::uint64_t outputTransitionEpoch =
            OW::RuntimeOutputTransitionEpoch();
        if (connectionEpoch == 0 || !OW::ProcessConnection::IsConnected()) {
            ResetMagneticTriggerQuantization(state, "connection_unavailable");
            return;
        }
        if (InputSequenceBlocksAim("magnetic_trigger")) {
            ResetMagneticTriggerQuantization(state, "sequence_preempted");
            return;
        }
        ObserveReleaseFireInput(state);
        UpdateFlickShotCooldown(state);
        if (!IsAimKeyPressed()) {
            ResetTrackingSession(state);
            return;
        }
        if (OW::Config::reloading) {
            ResetMagneticTriggerQuantization(state, "reloading");
            return;
        }

        const TrackingSessionIdentity sessionIdentity = CurrentTrackingSessionIdentity();
        EnsureTrackingSession(state, sessionIdentity);
        if (TrackingHoldTimedOut(state)) {
            ResetMagneticTriggerQuantization(state, "session_timeout_held");
            static DWORD lastAssistTimeoutLogTick = 0;
            const DWORD now = GetTickCount();
            if (lastAssistTimeoutLogTick == 0 || now - lastAssistTimeoutLogTick >= 250) {
                Diagnostics::Aim("magnetic_trigger skipped reason=session_timeout_held hero=0x%llX slot=%d key=%d",
                    static_cast<unsigned long long>(sessionIdentity.heroId),
                    sessionIdentity.slotIndex + 1,
                    sessionIdentity.aimKey);
                lastAssistTimeoutLogTick = now;
            }
            OW::TargetingDetail::ResetTargetLockRuntime();
            return;
        }

        g_flickAttempts++;
        const int behavior = OW::Config::ClampAimBehaviorIndex(OW::Config::View_aimBehavior());
        const c_entity localAtEntry = LocalEntity();
        Diagnostics::Aim("magnetic_trigger.enter originSens=%.6f shooted=%d reloading=%d trackingScale=%.6f baseSpeed=%.6f method=%d acceleration=%.6f prediction=%d targetDelay=%d",
            origin_sens,
            OW::Config::shooted ? 1 : 0,
            OW::Config::reloading ? 1 : 0,
            OW::Config::View_Tracking_smooth(),
            OW::Config::AimBehaviorBaseSpeed(behavior),
            OW::Config::AimBehaviorMethod(behavior),
            OW::Config::AimBehaviorAcceleration(behavior),
            OW::Config::View_Prediction() ? 1 : 0,
            OW::Config::targetdelay ? 1 : 0);



        if (OW::Config::aimDryRun) {
            ResetMagneticTriggerQuantization(state, "dry_run");
            static DWORD lastDryRunLog = 0;
            const DWORD now = GetTickCount();
            if (now - lastDryRunLog >= static_cast<DWORD>(OW::Config::aimDryRunLogIntervalMs)) {
                lastDryRunLog = now;
                const Vector3 vec = OW::GetVector3(OW::Config::View_Prediction());
            OW::AimCandidateConfig selectedSettings3(OW::Config::selectedAimHero);
                if (!IsZeroVector(vec)) {
                    float deadzoneDistance = 0.0f;
                    const float deadzoneDampingScale = TrackingDeadzoneDampingScale(vec, &deadzoneDistance);
                    PrepareAimCharge(state);
            AimData aim = BuildAimData(
                        vec,
                        false,
                        OW::Config::AimBehaviorSmoothInput(behavior, OW::Config::View_Tracking_smooth(), deadzoneDampingScale),
                        OW::Config::AimBehaviorAcceleration(behavior));
                    const float hitWindow = OW::Config::aimbotEffectiveHitWindow;
                    const bool hitBeforeMove = OW::in_range(aim.local_angle, aim.target_angle, aim.local_pos, vec, hitWindow);
                    const bool hitAfterMove = !hitBeforeMove &&
                        deadzoneDampingScale > 0.0f &&
                        OW::in_range(aim.smoothed_angle, aim.target_angle, aim.local_pos, vec, hitWindow);
                    const Vector3 rawDelta = aim.target_angle - aim.local_angle;
                    Vector3 previewDelta{};
                    if (!hitBeforeMove &&
                        deadzoneDampingScale > 0.0f &&
                        !IsZeroVector(aim.smoothed_angle)) {
                        previewDelta = aim.smoothed_angle - aim.local_angle;
                    }
                    const float pitchScale = std::clamp(
                        OW::Config::View_aimbotPitchScale(),
                        0.1f,
                        3.0f);
                    OW::MouseMoveQuantizationState previewState{};
                    const OW::MouseMoveQuantizationResult preview =
                        OW::QuantizeMouseMoveCounts(
                            -previewDelta.Y * OW::Config::OutputYawCountsPerRadian(),
                            previewDelta.X * OW::Config::OutputPitchCountsPerRadian() * pitchScale,
                            -rawDelta.Y * OW::Config::OutputYawCountsPerRadian(),
                            rawDelta.X * OW::Config::OutputPitchCountsPerRadian() * pitchScale,
                            previewState,
                            !hitBeforeMove);
                    Diagnostics::Aim("dryrun.magnetic_trigger local_angle_deg=(%.4f,%.4f) target_angle_deg=(%.4f,%.4f) delta_deg=(%.4f,%.4f) hitbox=%.4f would_fire=%d inside_hit_window=%d would_enqueue=%d preview_counts=(%d,%d) forced_min_step=%d post_move_hit=%d deadzoneDistance=%.3f deadzoneScale=%.3f target_pos=(%.1f,%.1f,%.1f)",
                        RAD2DEG(aim.local_angle.X), RAD2DEG(aim.local_angle.Y),
                        RAD2DEG(aim.target_angle.X), RAD2DEG(aim.target_angle.Y),
                        RAD2DEG(aim.target_angle.X - aim.local_angle.X), RAD2DEG(aim.target_angle.Y - aim.local_angle.Y),
                        hitWindow,
                        hitBeforeMove ? 1 : 0,
                        hitBeforeMove ? 1 : 0,
                        (!hitBeforeMove &&
                            (preview.pixelX != 0 || preview.pixelY != 0)) ? 1 : 0,
                        preview.pixelX,
                        preview.pixelY,
                        preview.forcedMinimumStep ? 1 : 0,
                        hitAfterMove ? 1 : 0,
                        deadzoneDistance,
                        deadzoneDampingScale,
                        vec.X, vec.Y, vec.Z);
                } else {
                    Diagnostics::Aim("dryrun.magnetic_trigger no_target_vector targetIndex=%d entities=%zu",
                        OW::Config::Targetenemyi,
                        OW::TargetingDetail::SnapshotEntities().size());
                }
            }
            OW::RuntimeWorkerSleep(1);
            return;
        }

        if (!OW::Config::shooted && GlobalAimFirePolicyAllowsGeneratedFire())
            ArmDelayedShot(state);

        while (AimModeStructureCurrent() && OW::RuntimeWorkersRunning() && IsAimKeyPressed() &&
               OW::ProcessConnection::IsConnected() &&
               OW::ProcessConnection::ConnectionEpoch() == connectionEpoch &&
               OW::RuntimeOutputTransitionMatches(outputTransitionEpoch) &&
               !OW::Config::reloading &&
               !OW::ShouldBlockForActiveSequence(ExecutionSource::GlobalAim)) {
            OW::consumerFrame=OW::SnapshotFrame();
            if(CompleteReleasedAimAtCrosshair(state,false)) break;
            UpdateFlickShotCooldown(state);
            const AimFirePhase::Phase firePhase = ResolveMagneticFirePhase(state);
            if (TrackingHoldTimedOut(state)) {
                ResetMagneticTriggerQuantization(state, "session_timeout");
                break;
            }

            const Vector3 vec = OW::GetVector3(OW::Config::View_Prediction());
            OW::AimCandidateConfig selectedSettings4(OW::Config::selectedAimHero);
            if (IsZeroVector(vec)) {
                if(CompleteReleasedAimAtCrosshair(state,true)) break;
                ResetMagneticTriggerQuantization(state, "no_target");
                Diagnostics::Aim("magnetic_trigger no_move reason=no_target_vector targetIndex=%d entities=%zu",
                    OW::Config::Targetenemyi,
                    OW::TargetingDetail::SnapshotEntities().size());
                OW::RuntimeWorkerSleep(1);
                RunAutoScaleFov();
                if (ShouldYieldToSecondaryAim()) break;
                continue;
            }

            c_entity target{};
            if (!IsPrimaryTargetActionable(target)) {
                if(CompleteReleasedAimAtCrosshair(state,true)) break;
                ResetMagneticTriggerQuantization(state, "target_not_actionable");
                Diagnostics::Aim("magnetic_trigger no_move reason=target_not_actionable targetIndex=%d vec=(%.9f,%.9f,%.9f)",
                    OW::Config::Targetenemyi,
                    vec.X,
                    vec.Y,
                    vec.Z);
                OW::RuntimeWorkerSleep(1);
                RunAutoScaleFov();
                if (ShouldYieldToSecondaryAim()) break;
                continue;
            }

            PrepareMagneticTriggerQuantization(
                state,
                state.trackingSessionGeneration,
                connectionEpoch,
                TwoStageEntityKey(target));

            if (!TargetDelayReady(&state, true, true))
                continue;
            if (!FlickTrajectoryWaitReady(state, target)) {
                OW::RuntimeWorkerSleep(1);
                continue;
            }
            if (!OW::Config::shooted && GlobalAimFirePolicyAllowsGeneratedFire())
                PrimeDelayedShot(state);

            const Vector3 aimTarget = vec;
            float deadzoneDistance = 0.0f;
            const float deadzoneDampingScale = TrackingDeadzoneDampingScale(aimTarget, &deadzoneDistance);
            const float smoothInput = OW::Config::AimBehaviorSmoothInput(
                behavior,
                OW::Config::View_Tracking_smooth() * firePhase.trackingScale,
                deadzoneDampingScale);
            PrepareAimCharge(state);
            AimData aim = BuildAimData(
                aimTarget,
                false,
                smoothInput,
                OW::Config::AimBehaviorAcceleration(behavior));
            ApplyAiAimNoise(aim.smoothed_angle, 500.f, true);
            if (!IsZeroVector(aim.smoothed_angle)) {
                Vector3 phasedDelta = aim.smoothed_angle - aim.local_angle;
                phasedDelta.X *= firePhase.pitchScale; // pitch drives screen Y
                phasedDelta.Y *= firePhase.yawScale;   // yaw drives screen X
                aim.smoothed_angle = aim.local_angle + phasedDelta;
            }
            const float hitWindow = OW::Config::aimbotEffectiveHitWindow;
            const bool hitBeforeMove = OW::in_range(
                aim.local_angle, aim.target_angle, aim.local_pos, aimTarget, hitWindow);
            ReportReleasedAimProgress(state,aim,aimTarget,hitWindow,hitBeforeMove);
            bool hitAfterMove = hitBeforeMove;

            if (!OW::Config::shooted &&
                GlobalAimFirePolicyAllowsGeneratedFire() &&
                DelayedShotTimedOut(state)) {
                const c_entity local = LocalEntity();
                ResetMagneticTriggerQuantization(state, "delayed_shot_timeout");
                Diagnostics::Aim("magnetic_trigger delayed_shot timeout target=%d localHero=0x%llX",
                    OW::Config::Targetenemyi,
                    static_cast<unsigned long long>(local.HeroID));
                if(!FirePrimaryNormal()) {OW::RuntimeWorkerSleep(1);continue;}
                StampFlickFire(state);
                OW::Config::shooted = true;
                continue;
            }

            if (hitBeforeMove) {
                ResetMagneticTriggerQuantization(state, "inside_hit_window");
                Diagnostics::Aim("magnetic_trigger.tick inside_hit_window=1 forced_min_step=0 mouse_enqueue=0 hitbox=%.4f",
                    hitWindow);
            } else {
                Vector3 outputAngle = aim.smoothed_angle;
                if (deadzoneDampingScale <= 0.0f ||
                    IsZeroVector(outputAngle) ||
                    !std::isfinite(outputAngle.X) ||
                    !std::isfinite(outputAngle.Y) ||
                    !std::isfinite(outputAngle.Z)) {
                    outputAngle = aim.local_angle;
                }
                if (deadzoneDampingScale <= 0.0f) {
                    OW::ResetAimSmoothingState();
                    if (OW::Config::aimVerboseLog) {
                        Diagnostics::Aim("magnetic_trigger.deadzone_fallback distancePx=%.3f radiusPx=%.3f inside_hit_window=0 fallback=raw_target_delta",
                            deadzoneDistance,
                            OW::Config::ClampTrackingDeadzonePixels(
                                OW::Config::View_aimbotTrackingDeadzone()));
                    }
                }

                const Vector3 rawDelta = aim.target_angle - aim.local_angle;
                const bool rawDeltaValid =
                    std::isfinite(rawDelta.X) &&
                    std::isfinite(rawDelta.Y) &&
                    std::isfinite(rawDelta.Z) &&
                    (std::fabs(rawDelta.X) > 0.0000001f ||
                        std::fabs(rawDelta.Y) > 0.0000001f);
                if (rawDeltaValid) {
                    Vector3 phaseFallbackDelta = rawDelta;
                    phaseFallbackDelta.X *= firePhase.pitchScale;
                    phaseFallbackDelta.Y *= firePhase.yawScale;
                    const bool phaseFallbackValid =
                        std::isfinite(phaseFallbackDelta.X) &&
                        std::isfinite(phaseFallbackDelta.Y) &&
                        (std::fabs(phaseFallbackDelta.X) > 0.0000001f ||
                            std::fabs(phaseFallbackDelta.Y) > 0.0000001f);
                    const Vector3 phaseFallbackTarget =
                        aim.local_angle + phaseFallbackDelta;
                    const Vector3 outputDelta = outputAngle - aim.local_angle;
                    const bool hasSmoothedDelta =
                        std::isfinite(outputDelta.X) &&
                        std::isfinite(outputDelta.Y) &&
                        std::isfinite(outputDelta.Z) &&
                        (std::fabs(outputDelta.X) > 0.0000001f ||
                            std::fabs(outputDelta.Y) > 0.0000001f);
                    const OW::MouseMoveQuantizationResult moveResult =
                        MoveMagneticTriggerAimDelta(
                            state,
                            aim.local_angle,
                            outputAngle,
                            phaseFallbackTarget);
                    if (moveResult.dispatched)
                        g_trackingMoves++;
                    if (hasSmoothedDelta) {
                        hitAfterMove = OW::in_range(
                            outputAngle,
                            aim.target_angle,
                            aim.local_pos,
                            aimTarget,
                            hitWindow);
                    }
                    static DWORD lastQuantizationTickLog = 0;
                    const DWORD quantizationLogNow = GetTickCount();
                    if (OW::Config::aimVerboseLog ||
                        lastQuantizationTickLog == 0 ||
                        quantizationLogNow - lastQuantizationTickLog >= 250) {
                        Diagnostics::Aim("magnetic_trigger.tick inside_hit_window=0 forced_min_step=%d mouse_enqueue=%d counts=(%d,%d) hitbox=%.4f deadzoneDistance=%.3f deadzoneScale=%.3f phaseScale=(track=%.3f yaw=%.3f pitch=%.3f) phase=(pause=%d recovery=%d prefire=%d ready=%d) hitAfter=%d",
                            moveResult.forcedMinimumStep ? 1 : 0,
                            moveResult.dispatched ? 1 : 0,
                            moveResult.pixelX,
                            moveResult.pixelY,
                            hitWindow,
                            deadzoneDistance,
                            deadzoneDampingScale,
                            firePhase.trackingScale,
                            firePhase.yawScale,
                            firePhase.pitchScale,
                            firePhase.paused ? 1 : 0,
                            firePhase.recovering ? 1 : 0,
                            firePhase.preFireBoost ? 1 : 0,
                            firePhase.readyToFire ? 1 : 0,
                            hitAfterMove ? 1 : 0);
                        lastQuantizationTickLog = quantizationLogNow;
                    }
                    if (phaseFallbackValid &&
                        moveResult.pixelX == 0 &&
                        moveResult.pixelY == 0) {
                        Diagnostics::Aim("magnetic_trigger invariant_warning outside_hit_window_zero_counts=1 raw_delta=(%.9f,%.9f,%.9f)",
                            rawDelta.X,
                            rawDelta.Y,
                            rawDelta.Z);
                    }
                    if (moveResult.dispatched) {
                        RunCloseRangeActions(
                            aimTarget,
                            OW::OutputOwnerSource::GlobalAim);
                    }
                } else {
                    ResetMagneticTriggerQuantization(state, "invalid_raw_target_delta");
                    Diagnostics::Aim("magnetic_trigger no_move reason=invalid_raw_target_delta inside_hit_window=0 raw_delta=(%.9f,%.9f,%.9f)",
                        rawDelta.X,
                        rawDelta.Y,
                        rawDelta.Z);
                }
            }

            if (!OW::Config::shooted &&
                GlobalAimFirePolicyAllowsGeneratedFire() &&
                firePhase.readyToFire && hitBeforeMove) {
                ResetMagneticTriggerQuantization(state, "inside_hit_window_fire");
                if (OW::Config::aimVerboseLog) {
                    Diagnostics::Aim("magnetic_trigger.fire hitbox_check=passed before=%d after=%d",
                        hitBeforeMove ? 1 : 0,
                        hitAfterMove ? 1 : 0);
                }
                SetSensitivityLocked(true, origin_sens);
                const bool submitted=FirePrimaryNormal();
                SetSensitivityLocked(false, origin_sens);
                if(!submitted) {OW::RuntimeWorkerSleep(1);continue;}
                StampFlickFire(state);
                ++g_flickFires;
                SetSensitivityLocked(false, origin_sens);
                OW::Config::shooted = true;
                if (OW::Config::dontshot)
                    OW::Config::shotcount++;
                break;
            }

            if (!OW::Config::shooted &&
                GlobalAimFirePolicyAllowsGeneratedFire() &&
                OW::Config::dontshot &&
                OW::Config::shotcount >= OW::Config::shotmanydont &&
                OW::in_range(aim.local_angle, aim.target_angle, aim.local_pos, aimTarget, OW::Config::missbox)) {
                const int previousShotCount = OW::Config::shotcount;
                const c_entity local = LocalEntity();
                ResetMagneticTriggerQuantization(state, "dontshot_forced_fire");
                Diagnostics::Aim("magnetic_trigger dontshot forced_fire target=%d previousShotCount=%d missbox=%.6f",
                    OW::Config::Targetenemyi,
                    previousShotCount,
                    OW::Config::missbox);
                if(!FirePrimaryNormal()) {OW::RuntimeWorkerSleep(1);continue;}
                OW::Config::shotcount = 0;
                StampFlickFire(state);
                OW::Config::shooted = true;
                continue;
            }

            if (LocalEntity().PlayerHealth < OW::Config::SkillHealth) break;

            OW::RuntimeWorkerSleep(1);
            RunAutoScaleFov();
            if (ShouldYieldToSecondaryAim()) break;
        }

        if (!IsAimKeyPressed()) {
            ResetTrackingSession(state);
        } else {
            const char* exitReason = "loop_exit";
            if (!OW::ProcessConnection::IsConnected() ||
                OW::ProcessConnection::ConnectionEpoch() != connectionEpoch) {
                exitReason = "connection_generation_changed";
            } else if (!OW::RuntimeOutputTransitionMatches(outputTransitionEpoch)) {
                exitReason = "output_transition_changed";
            } else if (OW::Config::reloading) {
                exitReason = "reloading";
            } else if (OW::Config::shooted) {
                exitReason = "fire_finished";
            } else if (OW::ShouldBlockForActiveSequence(ExecutionSource::GlobalAim)) {
                exitReason = "sequence_preempted";
            } else if (ShouldYieldToSecondaryAim()) {
                exitReason = "secondary_aim_preempted";
            } else if (TrackingHoldTimedOut(state)) {
                exitReason = "session_timeout";
            }
            ResetMagneticTriggerQuantization(state, exitReason);
        }
    }





    inline FlickRepeat::SessionIdentity CurrentOrdinaryFlickIdentity() {
        const auto selected = CurrentTrackingSessionIdentity();
        return { selected.heroId, OW::Config::View_aimbotAttack(), selected.slotIndex, selected.aimKey,
            OW::Config::configLoadGeneration.load(std::memory_order_acquire),
            OW::ProcessConnection::ConnectionEpoch(), OW::RuntimeOutputTransitionEpoch(), selected.slotId };
    }

    inline bool PrepareOrdinaryFlickSession(RuntimeState& state) {
        const auto identity = CurrentOrdinaryFlickIdentity();
        if (identity.configGeneration & 1u) {
            EndOrdinaryFlickActivation(state);
            return false;
        }
        if (FlickRepeat::PrepareSession(state.ordinaryFlick, identity))
            OW::ResetAimSmoothingState();
        return true;
    }



    inline void RunFlick(RuntimeState& state, float origin_sens) {
        const std::uint64_t connectionEpoch =
            OW::ProcessConnection::ConnectionEpoch();
        const std::uint64_t outputTransitionEpoch =
            OW::RuntimeOutputTransitionEpoch();
        if (connectionEpoch == 0 || !OW::ProcessConnection::IsConnected())
            return;
        if (InputSequenceBlocksAim("flick"))
            return;
        const int behavior = OW::Config::ClampAimBehaviorIndex(OW::Config::View_aimBehavior());
        const c_entity localAtEntry = LocalEntity();
        const bool ordinaryFlick = behavior == OW::Config::kAimBehaviorFlick;
        const auto ordinaryIdentityAtEntry = ordinaryFlick
            ? CurrentOrdinaryFlickIdentity() : FlickRepeat::SessionIdentity{};
        const DWORD cadence = ordinaryFlick ? ResolveCurrentWeaponCadenceIntervalMs() : 0;
        const DWORD ordinaryInterval = cadence == 0 ? 0
            : (std::max)(cadence, ResolveFlickRestartDelayMs());
        if (ordinaryFlick) {
            if (!PrepareOrdinaryFlickSession(state) || state.ordinaryFlick.identity != ordinaryIdentityAtEntry)
                return;
            if (!IsAimKeyPressed() || OW::Config::reloading ||
                !OrdinaryFlickPhase(state).movementAllowed) {
                PauseOrdinaryFlickMovement(state);
                return;
            }
            OW::Config::shooted = false;
        }
        ObserveReleaseFireInput(state);
        if (!ordinaryFlick) UpdateFlickShotCooldown(state);
        if (!ordinaryFlick && FlickPostFireLockoutActive(state))
            return;
        if (!IsAimKeyPressed() || OW::Config::shooted || OW::Config::reloading)
            return;
        g_flickAttempts++;

        Diagnostics::Aim("flick.enter originSens=%.6f shooted=%d reloading=%d scale=%.6f baseSpeed=%.6f method=%d acceleration=%.6f prediction=%d",
            origin_sens,
            OW::Config::shooted ? 1 : 0,
            OW::Config::reloading ? 1 : 0,
            OW::Config::View_Flick_smooth(),
            OW::Config::AimBehaviorBaseSpeed(behavior),
            OW::Config::AimBehaviorMethod(behavior),
            OW::Config::AimBehaviorAcceleration(behavior),
            OW::Config::View_Prediction() ? 1 : 0);



        // ---- Dry-run mode: log diagnostic info, don't move cursor ----
        if (OW::Config::aimDryRun) {
            static DWORD lastDryRunLog = 0;
            DWORD now = GetTickCount();
            if (now - lastDryRunLog >= (DWORD)OW::Config::aimDryRunLogIntervalMs) {
                lastDryRunLog = now;
                const Vector3 vec = OW::GetVector3(OW::Config::View_Prediction());
            OW::AimCandidateConfig selectedSettings5(OW::Config::selectedAimHero);
                if (!IsZeroVector(vec)) {
                    PrepareAimCharge(state);
            AimData aim = BuildAimData(
                        vec,
                        true,
                        OW::Config::AimBehaviorSmoothInput(behavior, OW::Config::View_Flick_smooth()),
                        OW::Config::AimBehaviorAcceleration(behavior));
                    const float hitWindow = OW::Config::aimbotEffectiveHitWindow;
                    const bool wouldHit = OW::in_range(aim.local_angle, aim.target_angle, aim.local_pos, vec, hitWindow);
                    Diagnostics::Aim("dryrun.flick local_angle_deg=(%.4f,%.4f) target_angle_deg=(%.4f,%.4f) "
                        "delta_deg=(%.4f,%.4f) hitbox=%.4f would_hit=%d target_pos=(%.1f,%.1f,%.1f)",
                        RAD2DEG(aim.local_angle.X), RAD2DEG(aim.local_angle.Y),
                        RAD2DEG(aim.target_angle.X), RAD2DEG(aim.target_angle.Y),
                        RAD2DEG(aim.target_angle.X - aim.local_angle.X), RAD2DEG(aim.target_angle.Y - aim.local_angle.Y),
                        hitWindow, wouldHit ? 1 : 0,
                        vec.X, vec.Y, vec.Z);
                } else {
                    Diagnostics::Aim("dryrun.flick no_target_vector targetIndex=%d entities=%zu",
                        OW::Config::Targetenemyi,
                        OW::TargetingDetail::SnapshotEntities().size());
                }
            }
            OW::RuntimeWorkerSleep(1);
            return; // Don't actually move cursor
        }

        if (GlobalAimFirePolicyAllowsGeneratedFire())
            ArmDelayedShot(state);
        const DWORD sessionStartedTick = GetTickCount();

        while (AimModeStructureCurrent() && OW::RuntimeWorkersRunning() && IsAimKeyPressed() &&
               OW::ProcessConnection::IsConnected() &&
               OW::ProcessConnection::ConnectionEpoch() == connectionEpoch &&
               OW::RuntimeOutputTransitionMatches(outputTransitionEpoch) &&
               (ordinaryFlick || !OW::Config::shooted) &&
               !OW::Config::reloading &&
               !OW::ShouldBlockForActiveSequence(ExecutionSource::GlobalAim)) {
            OW::consumerFrame=OW::SnapshotFrame();
            if(CompleteReleasedAimAtCrosshair(state,false)) break;
            const FlickRepeat::Phase repeatPhase = ordinaryFlick
                ? OrdinaryFlickPhase(state) : FlickRepeat::Phase{};
            if (ordinaryFlick && !repeatPhase.movementAllowed) {
                PauseOrdinaryFlickMovement(state);
                break;
            }
            if (ordinaryFlick && CurrentOrdinaryFlickIdentity() != state.ordinaryFlick.identity)
                break;
            if (AimSessionTimedOut(sessionStartedTick, "flick"))
                break;
            if (LocalEntity().HeroID == OW::eHero::HERO_WIDOWMAKER && !IsInputVkDown(VK_RBUTTON)) {
                Diagnostics::Aim("flick no_move reason=widow_scope_not_held");
                if (ordinaryFlick) PauseOrdinaryFlickMovement(state);
                OW::RuntimeWorkerSleep(1);
                continue;
            }

            const Vector3 vec = OW::GetVector3(OW::Config::View_Prediction());
            OW::AimCandidateConfig selectedSettings6(OW::Config::selectedAimHero);
            if (IsZeroVector(vec)) {
                CompleteReleasedAimAtCrosshair(state,true);
                Diagnostics::Aim("flick break reason=no_target_vector targetIndex=%d entities=%zu",
                    OW::Config::Targetenemyi,
                    OW::TargetingDetail::SnapshotEntities().size());
                break;
            }

            c_entity target{};
            if (IsPrimaryTargetActionable(target)) {
                if (!TargetDelayReady(&state, true, true)) {
                    if (ordinaryFlick) PauseOrdinaryFlickMovement(state);
                    OW::RuntimeWorkerSleep(1);
                    continue;
                }
                if (!FlickTrajectoryWaitReady(state, target)) {
                    if (ordinaryFlick) PauseOrdinaryFlickMovement(state);
                    OW::RuntimeWorkerSleep(1);
                    continue;
                }
                if (GlobalAimFirePolicyAllowsGeneratedFire())
                    PrimeDelayedShot(state);

                const TwoStageAimPlan twoStagePlan = ResolveTwoStageAimPlan(vec, target, OW::Config::View_Prediction());
                const Vector3 aimTarget = twoStagePlan.active ? twoStagePlan.target : vec;
                const float smoothInput = OW::Config::AimBehaviorSmoothInput(
                    behavior,
                    OW::Config::View_Flick_smooth(),
                    twoStagePlan.smoothScale);
                if (ordinaryFlick && FlickRepeat::PrepareTarget(state.ordinaryFlick,
                    TwoStageEntityKey(target)))
                    OW::ResetAimSmoothingState();
                PrepareAimCharge(state);
                AimData aim = BuildAimData(
                    aimTarget,
                    true,
                    smoothInput,
                    OW::Config::AimBehaviorAcceleration(behavior),
                    twoStagePlan.methodOverride,
                    -1.0f,
                    true,
                    !repeatPhase.repeat,
                    twoStagePlan.methodOverride < 0); // An explicit stage uses its Slot motion, above hero overrides.
                if (ordinaryFlick && !aim.valid) {
                    PauseOrdinaryFlickMovement(state);
                    break;
                }
                if (repeatPhase.repeat) {
                    aim.smoothed_angle = FlickRepeat::Step(aim.local_angle, aim.target_angle,
                        OrdinaryFlickPhase(state).remainingMs,
                        OW::AimSmoothingDetail::ComputeDeltaTime(),
                        OW::Config::flickRepeatMaxSpeedDegPerSec,
                        OW::in_range(aim.local_angle, aim.target_angle, aim.local_pos,
                            aimTarget, OW::Config::aimbotEffectiveHitWindow));
                } else {
                    ApplyAiAimNoise(aim.smoothed_angle, 300.f, false);
                }
                const float hitWindow = OW::Config::aimbotEffectiveHitWindow;

                if (ordinaryFlick || !IsZeroVector(aim.smoothed_angle)) {
                    if (FlickRepeat::AllowsFirePath(ordinaryFlick, FlickRepeat::FirePath::DelayedTimeout) &&
                        GlobalAimFirePolicyAllowsGeneratedFire() &&
                        DelayedShotTimedOut(state)) {
                        const c_entity local = LocalEntity();
                        Diagnostics::Aim("flick delayed_shot timeout target=%d localHero=0x%llX",
                            OW::Config::Targetenemyi,
                            static_cast<unsigned long long>(local.HeroID));
                        if(!FirePrimaryNormal()) {OW::RuntimeWorkerSleep(1);continue;}
                        StampFlickFire(state);
                        OW::Config::shooted = true;
                        continue;
                    }

                    if (ordinaryFlick && CurrentOrdinaryFlickIdentity() != state.ordinaryFlick.identity)
                        break;
                    if (ordinaryFlick && repeatPhase.repeat && aim.smoothed_angle == aim.local_angle)
                        OW::ResetAimSmoothingState();
                    else
                        MoveAimDelta(aim.local_angle, aim.smoothed_angle);
                    if (OW::Config::aimVerboseLog) {
                        const float deltaDegX = RAD2DEG(aim.target_angle.X - aim.local_angle.X);
                        const float deltaDegY = RAD2DEG(aim.target_angle.Y - aim.local_angle.Y);
                        Diagnostics::Aim("flick.tick delta_deg=(%.4f,%.4f) hitbox=%.4f missbox=%.4f",
                            deltaDegX, deltaDegY, hitWindow, OW::Config::missbox);
                    }
                    const bool hitBeforeMove = OW::in_range(
                        aim.local_angle, aim.target_angle, aim.local_pos, aimTarget, hitWindow);
                    const bool hitAfterMove = OW::in_range(
                        aim.smoothed_angle, aim.target_angle, aim.local_pos, aimTarget, hitWindow);
                    ReportReleasedAimProgress(state,aim,aimTarget,hitWindow,hitBeforeMove);
                    if (!hitBeforeMove && hitAfterMove) {
                        Diagnostics::Aim("flick.fire predicted_post_move_hit target=%d hitbox=%.6f",
                            OW::Config::Targetenemyi,
                            hitWindow);
                    }
                    if (GlobalAimFirePolicyAllowsGeneratedFire() &&
                        hitBeforeMove &&
                        (!twoStagePlan.active || twoStagePlan.triggerOpen) &&
                        (!ordinaryFlick || FlickRepeat::AllowsFire(OrdinaryFlickPhase(state),
                            OW::Config::reloading ? FlickRepeat::FireStateEvidence::KnownProhibited
                                : FlickRepeat::FireStateEvidence::Missing,
                            OW::RuntimeWorkersRunning() && OW::HasFreshViewMatrices() &&
                                IsAimKeyPressed() && LocalEntity().SkillBase != 0 &&
                                CurrentOrdinaryFlickIdentity() == state.ordinaryFlick.identity &&
                                !OW::ShouldBlockForActiveSequence(ExecutionSource::GlobalAim),
                            hitBeforeMove))) {
                        if (OW::Config::aimVerboseLog) {
                            Diagnostics::Aim("flick.fire hitbox_check=passed before=%d after=%d",
                                hitBeforeMove ? 1 : 0,
                                hitAfterMove ? 1 : 0);
                        }
                        SetSensitivityLocked(true, origin_sens);
                        DWORD submittedAt = GetTickCount();
                        const bool submitted = FirePrimaryNormal(OW::OutputOwnerSource::GlobalAim,
                            kGlobalAimGeneratedFireOwnerKey, ordinaryFlick, &submittedAt,
                            ordinaryFlick ? &state.ordinaryFlick.identity : nullptr);
                        SetSensitivityLocked(false, origin_sens);
                        if (!submitted) {
                            if(ordinaryFlick) PauseOrdinaryFlickMovement(state);
                            break;
                        }
                        if (ordinaryFlick) {
                            if (!FlickRepeat::RecordSubmission(state.ordinaryFlick, submitted,
                                submittedAt, ordinaryInterval)) break;
                            StampFlickFire(state, submittedAt);
                            // RecordSubmission already cleared movement; clear the just-used controller once.
                            OW::ResetAimSmoothingState();
                        } else {
                            StampFlickFire(state);
                        }
                        g_flickFires++;
                        OW::Config::shooted = true;
                        if (!ordinaryFlick && OW::Config::dontshot) OW::Config::shotcount++;
                        break;
                    }

                    if (FlickRepeat::AllowsFirePath(ordinaryFlick, FlickRepeat::FirePath::ForcedMiss) &&
                        GlobalAimFirePolicyAllowsGeneratedFire() &&
                        OW::Config::dontshot &&
                        OW::Config::shotcount >= OW::Config::shotmanydont &&
                        OW::in_range(aim.local_angle, aim.target_angle, aim.local_pos, aimTarget, OW::Config::missbox)) {
                        const int previousShotCount = OW::Config::shotcount;
                        OW::Config::shotcount = 0;
                        const c_entity local = LocalEntity();
                        Diagnostics::Aim("flick dontshot forced_fire target=%d previousShotCount=%d missbox=%.6f",
                            OW::Config::Targetenemyi,
                            previousShotCount,
                            OW::Config::missbox);
                        if(!FirePrimaryNormal()) {OW::RuntimeWorkerSleep(1);continue;}
                        StampFlickFire(state);
                        OW::Config::shooted = true;
                        continue;
                    }
                } else {
                    Diagnostics::Aim("flick no_move reason=smoothed_angle_zero local=(%.9f,%.9f,%.9f) target=(%.9f,%.9f,%.9f)",
                        aim.local_angle.X,
                        aim.local_angle.Y,
                        aim.local_angle.Z,
                        aim.target_angle.X,
                        aim.target_angle.Y,
                        aim.target_angle.Z);
                }
            } else {
                if(CompleteReleasedAimAtCrosshair(state,true)) break;
                if (ordinaryFlick) PauseOrdinaryFlickMovement(state);
                Diagnostics::Aim("flick no_move reason=target_not_actionable targetIndex=%d vec=(%.9f,%.9f,%.9f)",
                    OW::Config::Targetenemyi,
                    vec.X,
                    vec.Y,
                    vec.Z);
            }

            OW::RuntimeWorkerSleep(1);
            RunAutoScaleFov();
            if (ShouldYieldToSecondaryAim()) break;
        }
        if (ordinaryFlick) PauseOrdinaryFlickMovement(state);
    }

    inline void RunGenjiBlade() {
        const std::uint64_t connectionEpoch =
            OW::ProcessConnection::ConnectionEpoch();
        const std::uint64_t outputTransitionEpoch =
            OW::RuntimeOutputTransitionEpoch();
        c_entity local = LocalEntity();
        if (connectionEpoch == 0 ||
            !OW::ProcessConnection::IsConnected() ||
            !OW::Config::GenjiBlade || !IsInputVkDown('Q') ||
            local.HeroID != OW::eHero::HERO_GENJI ||
            local.ultimate != 100.f) {
            return;
        }

        OW::Config::Qstarttime = GetTickCount();
        OW::Config::Qtime = OW::Config::Qstarttime;
        OW::Config::lastenemy = -1;
        OW::RuntimeWorkerSleep(1000);

        int detecttoggle = 0;
        int first = 1;
        float speed = 0.f;
        while (OW::RuntimeWorkersRunning() && OW::HasFreshViewMatrices() &&
               OW::Config::GenjiBlade &&
               OW::ProcessConnection::IsConnected() &&
               OW::ProcessConnection::ConnectionEpoch() == connectionEpoch &&
               OW::RuntimeOutputTransitionMatches(outputTransitionEpoch) &&
               (OW::Config::Qtime - OW::Config::Qstarttime) <= 7000) {
            OW::consumerFrame=OW::SnapshotFrame();
            local = LocalEntity();
            speed = !local.skillcd1 ? OW::Config::View_Tracking_smooth() : OW::Config::bladespeed;
            OW::Config::Qtime = GetTickCount();

            const Vector3 vec = OW::GetVector3forgenji();
            if (!IsZeroVector(vec)) {
                const float dist = CameraPosition().DistTo(vec);
                if (dist > 20.f) continue;

                AimData aim = BuildAimData(
                    vec,
                    false,
                    OW::Config::AimBehaviorSmoothInput(0, speed),
                    OW::Config::AimBehaviorAcceleration(0));
                if (!IsZeroVector(aim.smoothed_angle)) {
                    const float dist2 = CameraPosition().DistTo(vec);
                    if ((!local.skillcd1 && dist2 < 20.f) || dist2 < 7.f) {
                        MoveAimDelta(aim.local_angle, aim.smoothed_angle);
                    }
                    if (OW::RuntimeWorkersRunning() && OW::HasFreshViewMatrices() &&
                        !local.skillcd1 && OW::in_range(aim.local_angle, aim.target_angle, aim.local_pos, vec, 0.8f)) {
                        if (detecttoggle && !first) {
                            detecttoggle = 0;
                            OW::RuntimeWorkerSleep(50);
                            continue;
                        }
                        if (OW::ActionOutputSucceeded(OW::PulseAction(OW::GameAction::Ability1, 70)))
                            first = 0;
                    }
                    if (OW::in_range(aim.local_angle, aim.target_angle, aim.local_pos, vec, 1.f) && dist2 < 5.f)
                        ClickMouseButton(0);
                    if (local.skillcd1 != 0 && !detecttoggle) detecttoggle = 1;
                }
            }
            OW::RuntimeWorkerSleep(1);
            OW::Config::lastenemy = OW::Config::Targetenemyi;
        }
    }

    inline void RunAutoMelee() {
        if (!OW::RuntimeWorkersRunning() || !OW::HasFreshViewMatrices())
            return;
        if (!OW::Config::AutoMelee) return;

        const Vector3 vec = OW::GetVector3(false);
            OW::AimCandidateConfig selectedSettings7(OW::Config::selectedAimHero);
        c_entity target{};
        if (!IsZeroVector(vec) && CurrentTarget(target) && target.Team) {
            const float dist = CameraPosition().DistTo(vec);
            if (OW::Config::health <= OW::Config::meleehealth &&
                dist <= OW::Config::meleedistance &&
                !(target.skill1act && target.HeroID == OW::eHero::HERO_VENTURE)) {
                OW::PulseAction(OW::GameAction::Melee);
                OW::RuntimeWorkerSleep(1);
            }
        }
    }

    inline void RunAutoRmb() {
        if (!OW::RuntimeWorkersRunning() || !OW::HasFreshViewMatrices())
            return;
        if (!OW::Config::AutoRMB || OW::Config::View_aimbotRequireActionHeld()) return;

        const Vector3 vec = OW::GetVector3(false);
            OW::AimCandidateConfig selectedSettings8(OW::Config::selectedAimHero);
        c_entity target{};
        if (!IsZeroVector(vec) && CurrentTarget(target) && target.Team) {
            const float dist = CameraPosition().DistTo(vec);
            if (OW::Config::health <= OW::Config::AutoRMBhealth &&
                dist <= OW::Config::AutoRMBdistance &&
                !(target.skill1act && target.HeroID == OW::eHero::HERO_VENTURE)) {
                ClickMouseButton(1);
                OW::RuntimeWorkerSleep(1);
            }
        }
    }

    inline void RunAutoShiftGenji() {
        if (!OW::RuntimeWorkersRunning() || !OW::HasFreshViewMatrices())
            return;
        const std::uint64_t connectionEpoch =
            OW::ProcessConnection::ConnectionEpoch();
        const std::uint64_t outputTransitionEpoch =
            OW::RuntimeOutputTransitionEpoch();
        if (connectionEpoch == 0 || !OW::ProcessConnection::IsConnected())
            return;
        if (!OW::Config::AutoShiftGenji) return;

        const Vector3 vec = OW::GetVector3(false);
            OW::AimCandidateConfig selectedSettings9(OW::Config::selectedAimHero);
        c_entity target{};
        if (IsZeroVector(vec) || !CurrentTarget(target)) return;
        if (target.imort || target.barrprot) return;
        if (target.HeroID == 0x16dd || target.HeroID == 0x16ee) return;

        const c_entity local = LocalEntity();
        if (local.skillcd1) return;

        const float dist = CameraPosition().DistTo(vec);
        AimData aim = BuildAimData(
            vec,
            false,
            OW::Config::AimBehaviorSmoothInput(0, OW::Config::View_Tracking_smooth()),
            OW::Config::AimBehaviorAcceleration(0));
        if (OW::Config::health <= 50.f && dist <= 15.f) {
            if (OW::in_range(aim.local_angle, aim.target_angle, aim.local_pos, vec, 1.f))
                OW::PulseAction(OW::GameAction::Ability1, 40);
        } else if (OW::Config::health <= 80.f && dist >= 15.f && dist <= 17.f) {
            if (OW::in_range(aim.local_angle, aim.target_angle, aim.local_pos, vec, 1.f)) {
                if (OW::ActionOutputSucceeded(OW::PulseAction(OW::GameAction::Ability1))) {
                    OW::RuntimeWorkerSleep(500);
                    if (OW::RuntimeWorkersRunning() && OW::HasFreshViewMatrices() &&
                        OW::ProcessConnection::IsConnected() &&
                        OW::ProcessConnection::ConnectionEpoch() == connectionEpoch &&
                        OW::RuntimeOutputTransitionMatches(outputTransitionEpoch)) {
                        OW::PulseAction(OW::GameAction::Melee);
                    }
                }
            }
        }
    }

    inline void RunAutoSkill() {
        if (!OW::RuntimeWorkersRunning() || !OW::HasFreshViewMatrices())
            return;
        if (!OW::Config::AutoSkill) return;

        const c_entity local = LocalEntity();
        if (local.PlayerHealth > OW::Config::SkillHealth && OW::Config::skilled)
            OW::Config::skilled = false;
        else if (local.PlayerHealth < OW::Config::SkillHealth && OW::Config::skilled &&
                 local.PlayerHealth < OW::Config::lasthealth &&
                 local.HeroID != OW::eHero::HERO_DOOMFIST)
            OW::Config::skilled = false;

        if (local.PlayerHealth >= OW::Config::SkillHealth || OW::Config::skilled) return;

        const auto hID = local.HeroID;
        if (hID == OW::eHero::HERO_TRACER || hID == OW::eHero::HERO_SOMBRA ||
            hID == OW::eHero::HERO_ROADHOG || hID == OW::eHero::HERO_TORBJORN ||
            hID == OW::eHero::HERO_SOLDIER76 || hID == OW::eHero::HERO_VENTURE) {
            if (OW::ActionOutputSucceeded(OW::PulseAction(OW::GameAction::Ability2))) {
                OW::Config::skilled = true;
                OW::RuntimeWorkerSleep(1);
                OW::Config::lasthealth = local.PlayerHealth;
            }
        } else if (hID == OW::eHero::HERO_REAPER || hID == OW::eHero::HERO_MEI ||
                   hID == OW::eHero::HERO_JUNKERQUEEN || hID == OW::eHero::HERO_MOIRA ||
                   hID == OW::eHero::HERO_ZARYA) {
            if (OW::ActionOutputSucceeded(OW::PulseAction(OW::GameAction::Ability1))) {
                OW::Config::skilled = true;
                OW::RuntimeWorkerSleep(1);
                OW::Config::lasthealth = local.PlayerHealth;
            }
        } else if (hID == OW::eHero::HERO_WINSTON || hID == OW::eHero::HERO_ZENYATTA) {
            if (OW::ActionOutputSucceeded(OW::PulseAction(OW::GameAction::Ultimate))) {
                OW::Config::skilled = true;
                OW::RuntimeWorkerSleep(1);
                OW::Config::lasthealth = local.PlayerHealth;
            }
        }
    }

    inline void RunAutoShootCooldown() {
        const c_entity local = LocalEntity();
        if (!OW::Config::AutoShoot || !OW::Config::shooted ||
            (local.HeroID == OW::eHero::HERO_HANJO && !local.skill2act)) {
            return;
        }

        const int rectime = GetTickCount();
        if (OW::Config::lasttime == 0) OW::Config::lasttime = rectime;
        else if (rectime - OW::Config::lasttime >= OW::Config::Shoottime) {
            OW::Config::lasttime = 0;
            OW::Config::shooted = false;
        }

        if (OW::Config::reloading) {
            OW::Config::lasttime = 0;
            OW::Config::shooted = false;
        }
    }

    inline void ResetShootStateOnRelease(RuntimeState& state) {
        if (IsAimKeyPressed()) return;
        EndOrdinaryFlickActivation(state);

        OW::Config::shooted = false;
        OW::Config::lasttime = 0;
        if (OW::Config::reloading) {
            OW::Config::lasttime = 0;
            OW::Config::shooted = false;
        }
        OW::Config::Targetenemyi = -1;
        OW::TargetingDetail::ResetTargetLockRuntime();
        ResetTwoStageState();
        ResetTrackingSession(state);
    }

    inline void RunReaperReloadCancel() {
        if (!OW::RuntimeWorkersRunning() || !OW::HasFreshViewMatrices())
            return;
        const std::uint64_t connectionEpoch =
            OW::ProcessConnection::ConnectionEpoch();
        const std::uint64_t outputTransitionEpoch =
            OW::RuntimeOutputTransitionEpoch();
        const c_entity local = LocalEntity();
        if (connectionEpoch != 0 &&
            OW::ProcessConnection::IsConnected() &&
            local.HeroID == OW::eHero::HERO_REAPER &&
            OW::Config::reloading) {
            OW::RuntimeWorkerSleep(300);
            if (OW::RuntimeWorkersRunning() && OW::HasFreshViewMatrices() &&
                OW::ProcessConnection::IsConnected() &&
                OW::ProcessConnection::ConnectionEpoch() == connectionEpoch &&
                OW::RuntimeOutputTransitionMatches(outputTransitionEpoch)) {
                OW::PulseAction(OW::GameAction::Melee);
            }
        }
    }

    inline void RunSecondAim() {
        const std::uint64_t connectionEpoch =
            OW::ProcessConnection::ConnectionEpoch();
        const std::uint64_t outputTransitionEpoch =
            OW::RuntimeOutputTransitionEpoch();
        if (connectionEpoch == 0 || !OW::ProcessConnection::IsConnected())
            return;
        if (!OW::Config::secondaim) return;
        if (InputSequenceBlocksAim("secondaim"))
            return;

        const DWORD sessionStartedTick = GetTickCount();
        while (OW::RuntimeWorkersRunning() && IsSecondAimKeyPressed() &&
               OW::ProcessConnection::IsConnected() &&
               OW::ProcessConnection::ConnectionEpoch() == connectionEpoch &&
               OW::RuntimeOutputTransitionMatches(outputTransitionEpoch) &&
               !OW::Config::shooted2 &&
               !OW::ShouldBlockForActiveSequence(ExecutionSource::GlobalAim)) {
            OW::consumerFrame=OW::SnapshotFrame();
            if (AimSessionTimedOut(sessionStartedTick, "secondaim")) {
                break;
            }
            const Vector3 vec = OW::GetVector3aim2(ResolveCurrentAimSlotPredictionEnabled());
            c_entity target{};
            if (!IsZeroVector(vec) && CurrentTarget(target) &&
                !(target.skill2act && target.HeroID == OW::eHero::HERO_GENJI)) {
                AimData aim{};
                if (OW::Config::Tracking2) {
                    const int method = OW::Config::SecondaryAimMethod(0);
                    aim = BuildAimData(
                        vec,
                        false,
                        OW::Config::AimBehaviorSmoothInput(0, OW::Config::Tracking_smooth2),
                        OW::Config::RuntimeAimMethodAcceleration(method),
                        method);
                } else if (OW::Config::Flick2) {
                    const int method = OW::Config::SecondaryAimMethod(1);
                    aim = BuildAimData(
                        vec,
                        true,
                        OW::Config::AimBehaviorSmoothInput(1, OW::Config::Flick_smooth2),
                        OW::Config::RuntimeAimMethodAcceleration(method),
                        method);
                } else
                    aim = BuildAimData(vec, false, 1.0f, 0.0f);

                ApplyAiAimNoise(aim.smoothed_angle, 300.f, false);

                if (!IsZeroVector(aim.smoothed_angle)) {
                    RunCloseRangeActions(
                        vec,
                        OW::OutputOwnerSource::GlobalAim);
                    MoveAimDelta(aim.local_angle, aim.smoothed_angle);
                    if (OW::Config::Flick2 &&
                        GlobalAimFirePolicyAllowsGeneratedFire() &&
                        OW::in_range(aim.local_angle, aim.target_angle, aim.local_pos, vec, OW::Config::aimbotEffectiveHitWindow2)) {
                        if (OW::Config::aimVerboseLog) {
                            const OW::c_entity local = LocalEntity();
                            const OW::WeaponSpec* weapon = OW::ResolveWeaponSpec(local.HeroID, OW::Config::View_aimbotAttack());
                            Diagnostics::Aim("secondaim.fire action=%d keyMask=0x%X",
                                OW::Config::View_aimbotAttack(),
                                OW::ResolveGeneratedFireKeyMask(weapon, OW::Config::View_aimbotAttack()));
                        }
                        ClickConfiguredFire();
                        OW::RuntimeWorkerSleep(1);
                        OW::Config::shooted2 = true;
                    }
                }

                if (LocalEntity().PlayerHealth < OW::Config::SkillHealth) break;
            }
            OW::RuntimeWorkerSleep(1);
        }

        if (!IsSecondAimKeyPressed())
            OW::TargetingDetail::ResetTargetLockRuntime();

        if (OW::Config::shooted2 && !IsSecondAimKeyPressed())
            OW::Config::shooted2 = false;
    }

    inline bool RefreshAimScene(RuntimeState& state) {
        const auto scene=OW::EntitySceneGeneration();
        if(state.entityScene!=scene) {
            const bool requireNewActivation=state.releaseActivationDown||state.releaseOwnedDown||state.releasePending;
            ResetTrackingSession(state);EndOrdinaryFlickActivation(state);
            if(!ResetAimCharge(state)) return false;
            chargedAimPreset.reset();
            // Scene/motion state can restart while the physical key is still held.
            const auto heldKeys = state.aimHoldWindows;
            const auto physicalPresses=state.releasePresses;
            state=RuntimeState{};state.entityScene=scene;
            state.aimHoldWindows = heldKeys;
            state.releasePresses=physicalPresses;
            state.releaseNeedsActivationUp=requireNewActivation;
            state.releaseActivationDown=requireNewActivation;
        }
        return true;
    }

    inline void RunConfiguredAimBehavior(RuntimeState& state, float origin_sens) {
        if(state.releaseNeedsActivationUp) return;
        const auto* tuning=OW::Config::ActiveSlotBehaviorTuning();
        if(tuning && tuning->releaseToFire && tuning->autoCharge &&
            OW::Config::View_Flick() && OW::Config::UsesFlickFireControls(OW::Config::View_aimBehavior())) {
            if(OW::Config::reloading || !AimModeStructureCurrent()) {
                CancelConfiguredAimShot(state);return;
            }
            if(!IsAimActivationKeyDown()&&!state.releasePending) {
                ResetShootStateOnRelease(state);return;
            }
            if(!PrepareAimCharge(state) && (UsesReleaseKeyUpExecution()||!state.releaseShotLatched)) return;
            if(UsesReleaseKeyUpExecution()) {
                if(!state.releasePending) return; // Holding charges anywhere without moving the view.
                const auto target=OW::GetVector3(OW::Config::View_Prediction());
                if(CompleteReleasedAimAtCrosshair(state,IsZeroVector(target))) return;
            }
        }
        if(!HasEntitySnapshot()) return;
        if (OW::Config::View_Tracking()) RunTracking(state, origin_sens);
        else if (OW::Config::View_Flick()) {
            if (OW::Config::IsMagneticTriggerBehavior(OW::Config::View_aimBehavior()))
                RunMagneticTrigger(state, origin_sens);
            else
                RunFlick(state, origin_sens);
        }
    }

    inline void RunAimbotTick(RuntimeState& state, float& origin_sens) {
        OW::ScopedConsumptionFrame frame;
        if(!RefreshAimScene(state)) return;
        if(!OW::SnapshotLocalEntity().HasUsableBody(GetTickCount(),OW::EntitySceneGeneration())) {
            ResetTrackingTargetContinuity(state,"local_body_unavailable");
            EndOrdinaryFlickActivation(state);CancelConfiguredAimShot(state);
            OW::RuntimeOutputScheduler().CancelSource(OW::OutputOwnerSource::GlobalAim);
            OW::RuntimeOutputScheduler().CancelSource(OW::OutputOwnerSource::Trigger);
            return;
        }
        if (!OW::RuntimeWorkersRunning() || !OW::HasFreshViewMatrices()) {
            ObserveReleaseFireInput(state);
            ResetShootStateOnRelease(state);
            return;
        }
        const std::uint64_t connectionEpoch =
            OW::ProcessConnection::ConnectionEpoch();
        const std::uint64_t outputTransitionEpoch =
            OW::RuntimeOutputTransitionEpoch();
        if (connectionEpoch == 0 || !OW::ProcessConnection::IsConnected())
            return;
        if (InputSequenceBlocksAim("aimbot_tick"))
            return;

        if (OW::Config::AntiAFK) {
            OW::PulseAction(OW::GameAction::MoveForward);
            OW::RuntimeWorkerSleep(1000);
            if (!OW::RuntimeWorkersRunning() || !OW::HasFreshViewMatrices() ||
                !OW::ProcessConnection::IsConnected() ||
                OW::ProcessConnection::ConnectionEpoch() != connectionEpoch ||
                !OW::RuntimeOutputTransitionMatches(outputTransitionEpoch)) {
                return;
            }
        }

        OW::RefreshAutoGameMouseSensitivity();

        if (!HasEntitySnapshot()) {
            if (IsAimKeyPressed()) {
                static DWORD lastNoSnapshotLogTick = 0;
                const DWORD now = GetTickCount();
                if (lastNoSnapshotLogTick == 0 || now - lastNoSnapshotLogTick >= 1000) {
                    Diagnostics::Aim("aimbot_tick target_search_skipped reason=no_entity_snapshot");
                    lastNoSnapshotLogTick = now;
                }
            }
            // Trigger still samples its own hotkey; release Aim can charge without targets.
        }

        MaintainSensitivity(origin_sens);

        {
            ScopedHeroTriggerPresetOverride triggerPresetOverride;
            if (OW::Config::View_triggerbot()) RunTriggerbot(false, origin_sens);
            if (OW::Config::View_triggerbot2()) RunTriggerbot(true, origin_sens);
        }

        RunConfiguredAimBehavior(state, origin_sens);
        if(!HasEntitySnapshot()) return;

        if (!OW::RuntimeWorkersRunning() || !OW::HasFreshViewMatrices()) {
            ObserveReleaseFireInput(state);
            ResetShootStateOnRelease(state);
            return;
        }
        RunGenjiBlade();
        if (!OW::RuntimeWorkersRunning() || !OW::HasFreshViewMatrices()) {
            ObserveReleaseFireInput(state);
            ResetShootStateOnRelease(state);
            return;
        }
        RunAutoScaleFov();
        RunAutoMelee();
        RunAutoRmb();
        RunAutoShiftGenji();
        RunAutoSkill();
        RunAutoShootCooldown();
        ResetShootStateOnRelease(state);
        RunReaperReloadCancel();
        RunSecondAim();
    }

    inline void RunAimbotTickWithHeroPreset(RuntimeState& state, float& origin_sens) {
        if (OW::Config::configLoadGeneration.load(std::memory_order_acquire) & 1u) {
            EndOrdinaryFlickActivation(state);CancelConfiguredAimShot(state);
            return;
        }
        if(!RefreshAimScene(state)) return;
        ScopedHeroPresetOverride heroPresetOverride(state);
        activeAimShotState=&state;
        MaintainConfiguredAimShot(state);
        ObserveAimHoldWindows(state);
        if (InputSequenceBlocksAim("aimbot_preset")) {
            CancelConfiguredAimShot(state);
            EndOrdinaryFlickActivation(state);
            return;
        }

        // Observe the effective per-hero configuration here, after the override.
        static DWORD lastGateReport = 0;
        const DWORD gateNow = GetTickCount();
        if (lastGateReport == 0 || gateNow - lastGateReport >= 5000) {
            lastGateReport = gateNow;
            const auto observedLocal = LocalEntity();
            Diagnostics::Info("[V2 behavior gates] hero=0x%llX menu=%d tracking=%d flick=%d behavior=%d key=%d input=%d localBody=%d matrix=%d entities=%d dryRun=%d",
                static_cast<unsigned long long>(observedLocal.HeroID), OW::Config::Menu ? 1 : 0,
                OW::Config::View_Tracking() ? 1 : 0, OW::Config::View_Flick() ? 1 : 0,
                OW::Config::View_aimBehavior(), OW::Config::View_aim_key(), IsAimKeyPressed() ? 1 : 0,
                observedLocal.HasUsableBody(gateNow, OW::EntitySceneGeneration()) ? 1 : 0,
                OW::HasFreshViewMatrices() ? 1 : 0, HasEntitySnapshot() ? 1 : 0,
                OW::Config::aimDryRun ? 1 : 0);
        }
        if (OW::Config::View_aimBehavior() != OW::Config::kAimBehaviorFlick || !IsAimKeyPressed() ||
            OW::Config::reloading)
            EndOrdinaryFlickActivation(state);
        RunAimbotTick(state, origin_sens);
        const bool activationOngoing=UsesReleaseKeyUpExecution()
            ? IsAimActivationKeyDown()||state.releasePending : IsAimKeyPressed();
        const bool unfinishedCharge=state.releaseOwnedDown||state.releasePending||chargedAimPreset.has_value();
        if((!activationOngoing&&unfinishedCharge)||OW::Config::reloading||!OW::RuntimeWorkersRunning()||!OW::HasFreshViewMatrices()||
           !AimModeStructureCurrent()||
           (OW::Config::IsMagneticTriggerBehavior(OW::Config::View_aimBehavior())&&IsAimKeyPressed()&&TrackingHoldTimedOut(state))||
           OW::ShouldBlockForActiveSequence(ExecutionSource::GlobalAim)) CancelConfiguredAimShot(state);
    }
#ifdef UNLEASHED_SELF_TESTS
#include "AimReleaseOutputSelfTest.inl"
#endif

}

void aimbot_thread() {
    Diagnostics::ScopedDmaCallsite::Push(Diagnostics::DmaCallsite::Aimbot);
    __try {
        AimbotDetail::RuntimeState state{};
        static float origin_sens = 0.f;
        static uint64_t totalTicks = 0;
        static DWORD lastSummaryTick = 0;

        while (OW::RuntimeWorkersRunning()) {
            if (!OW::ProcessConnection::IsConnected()) {
                AimbotDetail::EndOrdinaryFlickActivation(state);
                AimbotDetail::ObserveReleaseFireInput(state,true);
                OW::RuntimeWorkerSleep(100);
                continue;
            }

            totalTicks++;

            // Periodic diagnostic summary every 5 seconds
            DWORD now = GetTickCount();
            if (lastSummaryTick == 0 || now - lastSummaryTick >= 5000) {
                Diagnostics::Aim("aimbot.summary totalTicks=%llu trackingAttempts=%llu trackingMoves=%llu "
                    "flickAttempts=%llu flickFires=%llu dryRun=%d",
                    (unsigned long long)totalTicks,
                    (unsigned long long)g_trackingAttempts,
                    (unsigned long long)g_trackingMoves,
                    (unsigned long long)g_flickAttempts,
                    (unsigned long long)g_flickFires,
                    OW::Config::aimDryRun ? 1 : 0);
                lastSummaryTick = now;
            }

            AimbotDetail::RunAimbotTickWithHeroPreset(state, origin_sens);
            OW::RuntimeWorkerSleep(2);
        }
        AimbotDetail::ObserveReleaseFireInput(state,true);
    } __except (1) {
        OW::SetRuntimeOutputSessionEnabled(false);
        OW::SetRuntimeWorkersRunning(false);
        Diagnostics::Error("UN behavior worker stopped after a structured exception; reconnect required.");
    }
    Diagnostics::ScopedDmaCallsite::Pop();
}
