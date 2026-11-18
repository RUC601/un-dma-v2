#include <Windows.h>
#include <shellapi.h>

#include "Features/UI.hpp"
#include "ui/WorkspaceShell.hpp"
#include "ui/SlotSelector.hpp"
#include "ui/Text.hpp"
#include "ui/Theme.hpp"
#include "vision/ContourService.hpp"
using UI::Text::UiText;
using UI::Text::kUiLanguageOptions;
#include "output/DeviceCapabilities.hpp"
#include "game/profiles/ProfileState.hpp"
#include "Features/UiLabels.hpp"
#include "Features/UiValueFormat.hpp"
using namespace UI::ValueFormat;
#include "output/MockDiagnostics.hpp"
#include "output/OutputRuntime.hpp"
#include "Utils/Config.hpp"
#include "Game/HeroSkills.hpp"
#include "Game/HeroSettings.hpp"
#include "Game/Offsets.hpp"
#include "Game/Overwatch.hpp"
#include "Game/Target.hpp"
#include "Game/Structs.hpp"
#include "Game/WeaponSpec.hpp"
#include "Renderer/Overlay.hpp"
#include "Renderer/Renderer.hpp"
#include "Utils/Diagnostics.hpp"
#include "Utils/InputLabels.hpp"
#include "Utils/ProcessConnection.hpp"
#include "resource.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cfloat>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ui/EditorSupport.hpp"
#include "ui/AimPlanEditor.hpp"
#include "ui/BehaviorTuningEditor.hpp"
using namespace UI::Theme;

namespace {

enum class AimFireMode : int {
    KeepFiring = 0,
    SingleShot = 1,
    TrackingWithHold = 2,
    Legacy = 3,
};

constexpr const char* kAimFireModes[] = {
    "Keep Firing",
    "Single Shot",
    "Tracking With Hold",
    "Legacy policy (choose a new behavior)",
};

constexpr const char* kAimFireModesZh[] = {
    "持续按住",
    "单发",
    "跟踪并持续按住",
    "旧版策略（请选择新行为）",
};

AimFireMode AimFireModeFromPolicy(int policy)
{
    switch (OW::ClampFirePolicy(policy)) {
    case OW::FirePolicyType::KeepFiring:
        return AimFireMode::KeepFiring;
    case OW::FirePolicyType::SingleShot:
        return AimFireMode::SingleShot;
    case OW::FirePolicyType::TrackingWithHold:
        return AimFireMode::TrackingWithHold;
    default:
        return AimFireMode::Legacy;
    }
}

int AimFirePolicyFromMode(AimFireMode mode)
{
    switch (mode) {
    case AimFireMode::KeepFiring:
        return static_cast<int>(OW::FirePolicyType::KeepFiring);
    case AimFireMode::SingleShot:
        return static_cast<int>(OW::FirePolicyType::SingleShot);
    case AimFireMode::TrackingWithHold:
    default:
        return static_cast<int>(OW::FirePolicyType::TrackingWithHold);
    }
}

}

void UI::AimbotPage() {
    state.selectedTypeIndex = ClampHeroSelectionIndex(state.selectedTypeIndex);
    const HeroOption* selectedHero = &kHeroOptions[state.selectedTypeIndex];
    constexpr ActionSlotKind slotKind = ActionSlotKind::Aim;

    // ---- Enabled action-slot selector for the current hero ----
    const int previousSlot = state.aimHeroSegActive;
    const bool slotSelectorChanged = UIActionSlotSelector(slotKind, selectedHero->heroId);
    state.aimHeroSegActive = ActiveHeroPresetSlotIndex(slotKind);
    if (slotSelectorChanged || previousSlot != state.aimHeroSegActive)
        ApplySelectedTypePreset();

    bool presetChanged = false;
    bool slotEnabledChanged = false;
    OW::Config::HeroPreset activePreset{};
    OW::Config::AimScopeRequirement activeScopeRequirement =
        OW::Config::AimScopeRequirement::All;

    auto refreshActivePreset = [&]() {
        state.selectedTypeIndex = ClampHeroSelectionIndex(state.selectedTypeIndex);
        selectedHero = &kHeroOptions[state.selectedTypeIndex];
        const bool isGlobal = selectedHero->heroId == 0;
        activePreset = isGlobal
            ? OW::Config::GetSystemHeroPreset()
            : GetHeroActionPresetOrDefault(slotKind, selectedHero->heroId, state.aimHeroSegActive);
        activeScopeRequirement = OW::Config::AimScopeRequirement::All;
        if (!isGlobal) {
            OW::Config::HeroSlotPreset activeSlot{};
            if (OW::Config::TryGetHeroAimSlot(
                    selectedHero->heroId,
                    state.aimHeroSegActive,
                    activeSlot)) {
                activeScopeRequirement = activeSlot.scopeRequirement;
            }
        }
    };
    refreshActivePreset();
    OW::Config::ResolveHeroBehaviorMode(activePreset);
    // Prediction is now an automatic runtime capability.  Keep the legacy
    // fields readable for migration, but normalize the active Slot to the
    // single new semantic before it is saved again.
    if (activePreset.predictionMode != static_cast<int>(OW::PredictionOverrideMode::Auto) ||
        !activePreset.prediction) {
        activePreset.predictionMode = static_cast<int>(OW::PredictionOverrideMode::Auto);
        activePreset.prediction = true;
        presetChanged = true;
    }

    PushHeroActionSlotControlId(slotKind, selectedHero->heroId, state.aimHeroSegActive);

    ImGui::TextUnformatted(L("Basic Options"));
    UIGroupPair("Target Eligibility", [&]() {
        // Target Team
        SettingRow(L("Target Team"));
        PushControlWidth();
        const char* const kTeamLabels[] = { L("Enemies"), L("Allies"), L("All") };
        presetChanged |= UISelect("##aimTeam", &activePreset.targetTeam, kTeamLabels, IM_ARRAYSIZE(kTeamLabels));
        ImGui::PopItemWidth();
        // Enemy-only Slots do not need ally-specific controls.  Keep the
        // stored ally values for a later All/Ally selection, but only
        // expose them when the selected target team can include allies.
        if (activePreset.targetTeam != 0) {
            SettingRow(L("Ally key"));
            PushControlWidth();
            presetChanged |= UIKeyBinding("##allyKey", &activePreset.allyKey,
                                          KeyBindingStorage::AimActivationIndex);
            ImGui::PopItemWidth();
            SettingRow(L("Ally HP limit"));
            PushControlWidth();
            presetChanged |= UISlider("##allyHp", &activePreset.allyHealthMaxPercent,0.f,100.f,Number(0," %"));
            ImGui::PopItemWidth();
            SettingRow(L("Ally skeleton"));
            PushControlWidth();
            presetChanged |= UISkeletonBoneMultiSelect("##allyBones",&activePreset.allyBoneMask);
            ImGui::PopItemWidth();
        }
        SettingRow(L("Protection filter"));
        presetChanged |= UICheckbox("##targetableFilter",&activePreset.requireTargetable);
        if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",L("Skips targets with a fresh, explicitly known protection signal."));

        // Visibility is the user-facing form of the legacy ignoreInvisible bool.
        SettingRow(L("Visibility"));
        PushControlWidth();
        const char* const kVisibility[] = { L("Visible Only"), L("All") };
        int visibility = activePreset.ignoreInvisible ? 0 : 1;
        if (UISelect("##aimVisibility", &visibility, kVisibility, IM_ARRAYSIZE(kVisibility))) {
            activePreset.ignoreInvisible = visibility == 0;
            presetChanged = true;
        }
        ImGui::PopItemWidth();

        const bool leakShotSupported = activePreset.aimBehavior == OW::Config::kAimBehaviorFlick ||
            activePreset.aimBehavior == OW::Config::kAimBehaviorMagneticTrigger;
        SettingRow(L("Leak shot"));
        ImGui::BeginDisabled(!leakShotSupported && !activePreset.contourEnabled);
        presetChanged |= UICheckbox("##aimContour", &activePreset.contourEnabled);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", OW::Config::uiLanguage == 1
                ? (leakShotSupported ? "对原生不可见的目标，用新鲜采集画面确认露出部位。" : "漏打适用于 Flick 和磁性扳机。")
                : (leakShotSupported ? "Uses fresh capture frames to confirm exposed parts of hidden targets." : "Available with Flick and Magnetic Trigger."));
        if (activePreset.contourEnabled) {
            V2::Vision::Settings captureSettings;
            const auto captureFrame = V2::Vision::GetService().Snapshot(captureSettings);
            const auto captureStatus = V2::Vision::GetService().Status();
            const bool zh = OW::Config::uiLanguage == 1;
            const char* captureMessage = nullptr;
            if (!captureSettings.enabled || captureStatus.state == V2::Vision::ServiceState::Stopped)
                captureMessage = zh ? "漏打采集已停止" : "Leak shot capture stopped";
            else if (captureStatus.state == V2::Vision::ServiceState::Faulted)
                captureMessage = zh ? "漏打采集失败" : "Leak shot capture failed";
            else if (captureStatus.state == V2::Vision::ServiceState::Starting)
                captureMessage = zh ? "漏打采集连接中" : "Leak shot capture starting";
            else if (!captureFrame)
                captureMessage = zh ? "漏打等待新画面" : "Leak shot waiting for a fresh frame";
            if (captureMessage) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.64f, 0.25f, 1.0f));
                ImGui::TextWrapped("%s", captureMessage);
                ImGui::PopStyleColor();
            }
            if (ImGui::SmallButton(zh ? "打开采集设置" : "Open capture settings")) {
                state.activeTab = TAB_MISC;
                state.miscSubTab = 7;
            }
        }
    }, "Activation & Action", [&]() {
        slotEnabledChanged |= UIActionSlotEnabledCheckbox(slotKind, *selectedHero, "##aimSlotEnabled");

        SettingRow(L("Aim Activation Key"));
        PushControlWidth();
        presetChanged |= UIKeyBinding("##aimActivationKey", &activePreset.key,
                                      KeyBindingStorage::AimActivationIndex);
        ImGui::PopItemWidth();

        int scopeFilter = -1;
        if (OW::HeroUsesScopedStanceActions(selectedHero->heroId)) {
            const char* const scopeLabels[] = { L("All"), L("Scoped Only") };
            scopeFilter = static_cast<int>(activeScopeRequirement);
            SettingRow(L("Scope Requirement"));
            PushControlWidth();
            presetChanged |= UISelect("##aimScopeRequirement", &scopeFilter, scopeLabels, IM_ARRAYSIZE(scopeLabels));
            ImGui::PopItemWidth();
        }

        // Action Profile
        SettingRow(L("Action Profile"));
        PushControlWidth();
        presetChanged |= UIAttackActionSelect("##aimAttack", &activePreset.trigger.action,
                                              selectedHero->heroId, scopeFilter);
        ImGui::PopItemWidth();
    });

    UIGroupPair("Target Limits", [&]() {
        // Max Distance
        SettingRow(L("Max Distance"));
        PushControlWidth();
        presetChanged |= UISlider("##aimMaxDist", &activePreset.maxDistance, 0.0f, 500.0f, Number(0, " m"));
        ImGui::PopItemWidth();

        // Min Distance
        SettingRow(L("Min Distance"));
        PushControlWidth();
        presetChanged |= UISlider("##aimMinDist", &activePreset.minDistance, 0.0f, 500.0f, Number(0, " m"));
        ImGui::PopItemWidth();

        SettingRow(L("Max Head Distance"));
        PushControlWidth();
        presetChanged |= UISlider("##aimMaxHead", &activePreset.maxHeadDistance, 0.0f, 500.0f, Number(0, " m"));
        ImGui::PopItemWidth();

        SettingRow(L("Fast Target Limit"));
        PushControlWidth();
        presetChanged |= UISlider("##aimMaxTargetSpeed", &activePreset.maxTargetSpeedMps,
                                  0.0f, 100.0f, AtOrBelow("Unlimited", 0, Number(1, " m/s")));
        ImGui::PopItemWidth();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("0 means unlimited. Faster targets stay out of the Basic candidate set.");
        presetChanged |= UI::AimPlanEditor::HealthLimits(activePreset);
    }, "Behavior & Fire", [&]() {
        SettingRow(L("Behavior mode"));PushControlWidth();
        int chosenMode=activePreset.aimBehaviorPresetId;
        if(UISelectAimBehaviorPreset("##aimBehaviorMode",&chosenMode,-1,nullptr)) {
            OW::Config::StoreHeroBehaviorMode(activePreset);
            activePreset.aimBehaviorPresetId=chosenMode;
            OW::Config::ResolveHeroBehaviorMode(activePreset);presetChanged=true;
        }
        ImGui::PopItemWidth();
        if(ImGui::IsItemHovered())ImGui::SetTooltip("%s: %s + %s",L("Mode structure"),L(kAimBehavior[activePreset.aimBehavior]),L(kAimMethod[activePreset.aimMethod]));

        SettingRow(L("Fire behavior"));
        PushControlWidth();
        const int storedFirePolicy = activePreset.firePolicy;
        const AimFireMode currentFireMode = AimFireModeFromPolicy(storedFirePolicy);
        const char* const* fireModeLabels = OW::Config::uiLanguage == 1
            ? kAimFireModesZh : kAimFireModes;
        const int action = activePreset.trigger.action;
        const auto* weapon = OW::ResolveWeaponSpec(selectedHero->heroId, action);
        std::array<AimFireMode, 4> choices{};
        std::array<const char*, 4> labels{};
        int fireChoiceCount = 0;
        int selectedFireChoice = -1;
        const auto addFireChoice = [&](AimFireMode mode) {
            if (mode == currentFireMode) selectedFireChoice = fireChoiceCount;
            labels[fireChoiceCount] = fireModeLabels[static_cast<int>(mode)];
            choices[fireChoiceCount++] = mode;
        };
        if (OW::ResolveGeneratedFireMouseButton(weapon, action) >= 0) {
            addFireChoice(AimFireMode::KeepFiring);
            addFireChoice(AimFireMode::SingleShot);
        }
        if (activePreset.aimBehavior == OW::Config::kAimBehaviorTracking &&
            OW::ResolveTrackingHoldMouseButton(weapon, action) >= 0)
            addFireChoice(AimFireMode::TrackingWithHold);
        if (selectedFireChoice < 0)
            addFireChoice(currentFireMode);
        if (UISelect("##aimFireMode", &selectedFireChoice, labels.data(), fireChoiceCount) &&
            choices[selectedFireChoice] != AimFireMode::Legacy) {
            const AimFireMode fireMode = choices[selectedFireChoice];
            activePreset.firePolicy = AimFirePolicyFromMode(fireMode);
            activePreset.keepFiring = fireMode != AimFireMode::SingleShot;
            activePreset.autoshot = fireMode == AimFireMode::SingleShot;
            activePreset.requireActionHeld = false;
            presetChanged = true;
        }
        ImGui::PopItemWidth();
        if (ImGui::IsItemHovered()) {
            if (choices[selectedFireChoice] == AimFireMode::Legacy) {
                ImGui::SetTooltip(L("Legacy internal policy %s is still loaded. Select one of the three user-facing Fire behaviors to migrate this Slot."),
                                  OW::FirePolicyName(OW::ClampFirePolicy(storedFirePolicy)));
            } else {
                ImGui::SetTooltip("%s", OW::Config::uiLanguage == 1
                    ? "持续按住随激活键生效。单发每次按下最多提交一次。跟踪并持续按住随跟踪会话生效。"
                    : "Keep Firing follows the activation key. Single Shot submits at most one shot per press. Tracking With Hold follows the tracking session.");
            }
        }

        SettingRow(L("Skeleton"));
        PushControlWidth();
        presetChanged |= UISkeletonBoneMultiSelect("##aimSkeleton", &activePreset.aimBoneMask);
        ImGui::PopItemWidth();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", L("Choose All or combine groups, for example Head + Feet."));
        SettingRow(L("Bone selection"));
        const char* boneSelectionModes[] = { L("Only"), L("Prefer"), L("Closest") };
        activePreset.boneSelectionMode = std::clamp(activePreset.boneSelectionMode, 0, 2);
        PushControlWidth();
        presetChanged |= UISelect("##aimBoneSelectionMode", &activePreset.boneSelectionMode,
                                  boneSelectionModes, IM_ARRAYSIZE(boneSelectionModes));
        ImGui::PopItemWidth();
        if (activePreset.boneSelectionMode == 1) {
            SettingRow(L("Preferred bone"));
            const char* preferredBones[] = { L("Chest"), L("Head"), L("Neck") };
            activePreset.preferredBone = OW::Config::NormalizeAimBone(activePreset.preferredBone);
            PushControlWidth();
            presetChanged |= UISelect("##aimPreferredBone", &activePreset.preferredBone,
                                      preferredBones, IM_ARRAYSIZE(preferredBones));
            ImGui::PopItemWidth();
        }
    });

    ImGui::SetNextItemOpen(state.aimAdvanced, ImGuiCond_Always);
    state.aimAdvanced = ImGui::CollapsingHeader(L("Advanced Options"));
    if (state.aimAdvanced) {
        UIGroupPair("FOV & Prediction Entry", [&]() {
            const char* const kFovModeLabels[] = { L("Fixed"), L("Dynamic Preset") };
            activePreset.fovMode = OW::Config::ClampFovMode(activePreset.fovMode);
            if (activePreset.fovMode == OW::Config::kFovModeDynamicPreset &&
                !OW::Config::FindDynamicFovPreset(activePreset.dynamicFovPresetId)) {
                activePreset.fovMode = OW::Config::kFovModeFixed;
                activePreset.dynamicFovPresetId = -1;
            }

            SettingRow(L("FOV Mode"));
            PushControlWidth();
            if (UISelect("##aimFovMode", &activePreset.fovMode, kFovModeLabels, IM_ARRAYSIZE(kFovModeLabels))) {
                activePreset.fovMode = OW::Config::ClampFovMode(activePreset.fovMode);
                if (activePreset.fovMode == OW::Config::kFovModeFixed) {
                    activePreset.dynamicFovPresetId = -1;
                } else if (!OW::Config::Read_dynamicFovPresets().empty()) {
                    activePreset.dynamicFovPresetId = OW::Config::Read_dynamicFovPresets().front().id;
                }
                presetChanged = true;
            }
            ImGui::PopItemWidth();

            if (activePreset.fovMode == OW::Config::kFovModeDynamicPreset) {
                SettingRow(L("FOV Preset"));
                PushControlWidth();
                presetChanged |= UISelectDynamicFovPreset("##aimDynamicFovPreset",
                                                          &activePreset.dynamicFovPresetId,
                                                          false);
                ImGui::PopItemWidth();
                if (!OW::Config::FindDynamicFovPreset(activePreset.dynamicFovPresetId))
                    SettingTooltip(L("Create a Dynamic FOV preset in Misc first."));
            } else {
                SettingRow(L("FOV (deg)"));
                PushControlWidth();
                presetChanged |= UISlider("##aimFov", &activePreset.fov,
                                          OW::Config::kMinFovDeg, OW::Config::kMaxFovDeg, Number(0, " deg"));
                ImGui::PopItemWidth();
            }

            SettingRow(L("Predict FOV Entry"));
            presetChanged |= UICheckbox("##aimPredictFovEntry", &activePreset.predictFovEntry);
            if (activePreset.predictFovEntry) {
                SettingRow(L("Entry Horizon"));
                PushControlWidth();
                presetChanged |= UISlider("##aimFovEntryHorizon", &activePreset.fovEntryPredictionMs, 0.0f, 250.0f, Number(0, " ms"));
                ImGui::PopItemWidth();

                SettingRow(L("Outside Margin"));
                PushControlWidth();
                presetChanged |= UISlider("##aimFovEntryMargin", &activePreset.fovEntryMaxOutsideDeg, 0.0f, 15.0f, Number(1, " deg"));
                ImGui::PopItemWidth();
            }

            presetChanged |= UIPredictionAlgorithmSelect("##aimPredictionAlgorithm",
                &activePreset.predictionAlgorithm);

            if (UIActionButton(L("Prediction Data"), 0.0f))
                state.activeTab = TAB_PREDICTION;

        }, "Target Switching", [&]() {
            const char* const kTraceLabels[] = { L("Strict"), L("Relaxed"), L("Off") };
            const char* const kUnlockLabels[] = { L("Anytime"), L("On Release"), L("Never") };
            SettingRow(L("Trace Condition"));
            PushControlWidth();
            presetChanged |= UISelect("##aimTrace", &activePreset.traceCondition, kTraceLabels, IM_ARRAYSIZE(kTraceLabels));
            ImGui::PopItemWidth();

            // Retarget hysteresis
            SettingRow(L("Retarget Hysteresis"));
            PushControlWidth();
            presetChanged |= UISlider("##aimStick", &activePreset.stickiness, 0.0f, 100.0f, AtOrAbove("Max", 100, Number(1, " %")));
            ImGui::PopItemWidth();

            // Unlock Condition
            SettingRow(L("Unlock Condition"));
            PushControlWidth();
            presetChanged |= UISelect("##aimUnlock", &activePreset.unlockCondition, kUnlockLabels, IM_ARRAYSIZE(kUnlockLabels));
            ImGui::PopItemWidth();

            // Lock Time
            SettingRow(L("Lock Time"));
            PushControlWidth();
            presetChanged |= UISlider("##aimLockTime", &activePreset.lockTime, 0.0f, 100.0f, Number(0, " ms", 10));
            ImGui::PopItemWidth();

        });

        UIGroupPair("Target Ranking", [&]() {
            const char* const kPriorityLabels[] = { L("Lowest FOV"), L("Lowest HP"), L("Distance"), L("Smart") };
            SettingRow(L("Target Priority"));
            PushControlWidth();
            presetChanged |= UISelect("##aimPriority", &activePreset.priority, kPriorityLabels, IM_ARRAYSIZE(kPriorityLabels));
            ImGui::PopItemWidth();
            presetChanged |= UI::AimPlanEditor::Policy(activePreset, true, false);
        }, "Behavior Tuning", [&]() {
            SettingRow(L("Global response scale"));
            PushControlWidth();
            presetChanged |= UISlider("##slotBaseSpeed", &activePreset.baseSpeed, 0, 100, Number(1, " %"));
            ImGui::PopItemWidth();
            SettingRow(L("Hitbox Scale"));
            PushControlWidth();
            presetChanged |= UISlider("##aimHitbox", &activePreset.hitbox,
                                      OW::Config::kMinHitboxScalePercent,
                                      OW::Config::kMaxHitboxScalePercent, Number(0, " %"));
            ImGui::PopItemWidth();
            if (UIActionButton(L("Edit motion..."), 0.0f))
                ImGui::OpenPopup("##MotionEditor");
            ImGui::SameLine();
            if (UIActionButton(L("Edit hero overrides..."), 0.0f))
                ImGui::OpenPopup("##ExceptionEditor");
        });
        ImGui::SetNextWindowSizeConstraints(ImVec2(300,180),ImVec2(ImGui::GetMainViewport()->WorkSize.x-24,ImGui::GetMainViewport()->WorkSize.y-24));
        ImGui::SetNextWindowSize(ImVec2(650,650),ImGuiCond_FirstUseEver);
        if(ImGui::BeginPopup("##MotionEditor",ImGuiWindowFlags_NoSavedSettings)) {
            if(ImGui::Button(L("Restore mode defaults"))) {OW::Config::ResolveHeroBehaviorMode(activePreset,true);presetChanged=true;}
            presetChanged|=UI::BehaviorTuningEditor(activePreset,activePreset.aimBehavior,false);
            presetChanged|=UI::AimPlanEditor::Motion(activePreset.aimPlan.motion,false);
            if(activePreset.aimBehavior==OW::Config::kAimBehaviorFlick2nd && activePreset.flick2ndInnerMethod>=0 &&
                activePreset.flick2ndInnerMethod!=activePreset.aimMethod) {
                auto& motion=OW::Config::EnsureSlotMotion(activePreset,activePreset.flick2ndInnerMethod);
                if(ImGui::CollapsingHeader(L("Second stage motion parameters")))presetChanged|=UI::AimPlanEditor::Motion(motion,false);
            }
            ImGui::EndPopup();
        }
        ImGui::SetNextWindowSizeConstraints(ImVec2(300,180),ImVec2(ImGui::GetMainViewport()->WorkSize.x-24,ImGui::GetMainViewport()->WorkSize.y-24));
        ImGui::SetNextWindowSize(ImVec2(680,650),ImGuiCond_FirstUseEver);
        if(ImGui::BeginPopup("##ExceptionEditor",ImGuiWindowFlags_NoSavedSettings)) {
            presetChanged|=UI::AimPlanEditor::Editor(activePreset);
            ImGui::EndPopup();
        }
    }
    ImGui::Spacing();
    ImGui::TextDisabled("%s", L("Shared presets"));
    if (ImGui::SmallButton(L("Edit behavior preset"))) { state.activeTab = TAB_MISC; state.miscSubTab = 2; }
    ImGui::SameLine();
    if (ImGui::SmallButton(L("Edit method preset"))) { state.activeTab = TAB_MISC; state.miscSubTab = 2; }
    ImGui::SameLine();
    if (ImGui::SmallButton(L("Edit FOV preset"))) { state.activeTab = TAB_MISC; state.miscSubTab = 4; }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", L("Mode structure is shared. Slot parameters stay independent."));

    if(presetChanged) OW::Config::StoreHeroBehaviorMode(activePreset);
    if (selectedHero->heroId == 0) {
        if (presetChanged)
            OW::Config::SetSystemAimPreset(activePreset);
    } else {
        if (presetChanged) {
            SetHeroActionPreset(slotKind, selectedHero->heroId, state.aimHeroSegActive, activePreset);
            ApplyHeroActionPresetToGlobals(slotKind, activePreset);
        } else if (slotEnabledChanged &&
                   IsHeroActionSlotEnabled(slotKind, selectedHero->heroId, state.aimHeroSegActive)) {
            ApplyHeroActionPresetToGlobals(slotKind, activePreset);
        }
    }

    ImGui::PopID();
}

// =====================================================================
// UI::TriggerPage
// =====================================================================
