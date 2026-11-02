#include <Windows.h>
#include <shellapi.h>

#include "Features/UI.hpp"
#include "ui/WorkspaceShell.hpp"
#include "ui/SlotSelector.hpp"
#include "ui/Text.hpp"
#include "ui/Theme.hpp"
using UI::Text::UiText;
using UI::Text::kUiLanguageOptions;
#include "output/DeviceCapabilities.hpp"
#include "game/profiles/ProfileState.hpp"
#include "Features/UiLabels.hpp"
#include "Features/UiValueFormat.hpp"
using namespace UI::ValueFormat;
#include "output/MockBackend.hpp"
#include "output/OutputRuntime.hpp"
#include "Utils/Config.hpp"
#include "Game/HeroSkills.hpp"
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
using namespace UI::Theme;

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
    bool scopeRequirementChanged = false;
    OW::Config::HeroPreset activePreset{};
    OW::Config::AimScopeRequirement activeScopeRequirement =
        OW::Config::AimScopeRequirement::All;

    auto refreshActivePreset = [&]() {
        state.selectedTypeIndex = ClampHeroSelectionIndex(state.selectedTypeIndex);
        selectedHero = &kHeroOptions[state.selectedTypeIndex];
        const bool isGlobal = selectedHero->heroId == 0;
        activePreset = isGlobal
            ? MakeCurrentHeroActionPreset(slotKind)
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

    PushHeroActionSlotControlId(slotKind, selectedHero->heroId, state.aimHeroSegActive);

    slotEnabledChanged |= UIActionSlotEnabledCheckbox(slotKind, *selectedHero, "##aimSlotEnabled", kAimbotHeroLabelWidth);
    ImGui::Spacing();
    UITwoColumns([&]() {
        UIGroupBox("Behavior & Input");
            // Aim Behavior
            SettingRow("Aim Behavior", kAimbotLeftLabelWidth);
            PushControlWidth();
            if (UISelect("##aimBehavior", &activePreset.aimBehavior,
                         kAimBehavior, IM_ARRAYSIZE(kAimBehavior))) {
                activePreset.aimBehaviorPresetId = -1;
                ApplyAimBehaviorToHeroPreset(activePreset, activePreset.aimBehavior);
                presetChanged = true;
            }
            ImGui::PopItemWidth();

            SettingRow("Behavior Preset", kAimbotLeftLabelWidth);
            PushControlWidth();
            if (UISelectAimBehaviorPreset("##aimBehaviorPreset", &activePreset.aimBehaviorPresetId, -1)) {
                if (const OW::Config::AimBehaviorPreset* behaviorPreset =
                        OW::Config::FindAimBehaviorPreset(activePreset.aimBehaviorPresetId)) {
                    ApplyAimBehaviorToHeroPreset(activePreset, behaviorPreset->behavior);
                }
                presetChanged = true;
            }
            ImGui::PopItemWidth();

            // Aim activation key is stored per aim slot.
            SettingRow("Aim Activation Key", kAimbotLeftLabelWidth);
            PushControlWidth();
            presetChanged |= UISelect("##aimActivationKey", &activePreset.key,
                                      OW::Labels::kAimActivationKeys, OW::Labels::AimActivationKeyCount());
            ImGui::PopItemWidth();

            if (OW::Config::HeroUsesScopedWeaponActionSplit(selectedHero->heroId)) {
            // Scope Requirement
            SettingRow("Scope Requirement", kAimbotLeftLabelWidth);
            PushControlWidth();
            static const char* const kAimScopeRequirements[] = { "All", "Scoped Only" };
            int scopeRequirement = static_cast<int>(activeScopeRequirement);
            const bool scopeCanChange =
                OW::Config::HeroUsesScopedWeaponActionSplit(selectedHero->heroId);
            if (!scopeCanChange)
                ImGui::BeginDisabled();
            if (UISelect("##aimScopeRequirement",
                         &scopeRequirement,
                         kAimScopeRequirements,
                         IM_ARRAYSIZE(kAimScopeRequirements))) {
                activeScopeRequirement = OW::Config::NormalizeAimScopeRequirement(scopeRequirement);
                activePreset.trigger.action =
                    activeScopeRequirement == OW::Config::AimScopeRequirement::ScopedOnly
                    ? 2
                    : 0;
                scopeRequirementChanged = true;
                presetChanged = true;
            }
            if (!scopeCanChange)
                ImGui::EndDisabled();
            ImGui::PopItemWidth();

            }

            // Attack
            SettingRow("Attack", kAimbotLeftLabelWidth);
            PushControlWidth();
            if (UIAttackActionSelect("##aimAttack", &activePreset.trigger.action, selectedHero->heroId)) {
                activeScopeRequirement =
                    OW::Config::DefaultAimScopeRequirementForHeroAction(
                        selectedHero->heroId,
                        activePreset.trigger.action);
                presetChanged = true;
            }
            ImGui::PopItemWidth();

            SettingRow("Require Action Held", kAimbotLeftLabelWidth);
            const bool actionHasPhysicalMouseButton =
                OW::MouseButtonForAttackAction(activePreset.trigger.action) >= 0;
            if (!actionHasPhysicalMouseButton) {
                activePreset.requireActionHeld = false;
                ImGui::BeginDisabled();
            }
            presetChanged |= UICheckbox(
                "##aimRequireActionHeld",
                &activePreset.requireActionHeld);
            if (!actionHasPhysicalMouseButton)
                ImGui::EndDisabled();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(actionHasPhysicalMouseButton
                    ? "Aim only while the selected weapon or skill action is physically held."
                    : "This action has no physical mouse-button mapping.");

            // Fire Policy
            SettingRow("Fire Policy", kAimbotLeftLabelWidth);
            PushControlWidth();
            if (UISelect("##aimFirePolicy", &activePreset.firePolicy,
                         kFirePolicy, IM_ARRAYSIZE(kFirePolicy))) {
                activePreset.keepFiring = activePreset.firePolicy == 1;
                activePreset.autoshot = activePreset.firePolicy >= 2;
                presetChanged = true;
            }
            ImGui::PopItemWidth();

            SettingRow("Skeleton", kAimbotLeftLabelWidth);
            PushControlWidth();
            presetChanged |= UISkeletonBoneMultiSelect("##aimSkeleton", &activePreset.aimBoneMask);
            ImGui::PopItemWidth();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", L("Choose All or combine groups, for example Head + Feet."));

            // Speed scale
            SettingRow("Speed Scale", kAimbotLeftLabelWidth);
            PushControlWidth();
            presetChanged |= UISlider("##aimSmooth", &activePreset.smooth, 0.0f, 100.0f, Number(2, " %"));
            ImGui::PopItemWidth();

            if (OW::Config::UsesTrackingDeadzone(activePreset.aimBehavior)) {
                SettingRow("Deadzone", kAimbotLeftLabelWidth);
                PushControlWidth();
                presetChanged |= UISlider("##aimTrackingDeadzone", &activePreset.trackingDeadzone, 0.0f, 250.0f, Number(0, " px"));
                ImGui::PopItemWidth();
            }

        CloseGroupBox();
    }, [&]() {
        UIGroupBox("Range & Target");
            // FOV
            static const char* kFovModeLabels[] = { "Fixed", "Dynamic Preset" };
            activePreset.fovMode = OW::Config::ClampFovMode(activePreset.fovMode);
            if (activePreset.fovMode == OW::Config::kFovModeDynamicPreset &&
                !OW::Config::FindDynamicFovPreset(activePreset.dynamicFovPresetId)) {
                activePreset.fovMode = OW::Config::kFovModeFixed;
                activePreset.dynamicFovPresetId = -1;
            }

            SettingRow("FOV Mode", kAimbotLeftLabelWidth);
            PushControlWidth();
            if (UISelect("##aimFovMode", &activePreset.fovMode, kFovModeLabels, IM_ARRAYSIZE(kFovModeLabels))) {
                activePreset.fovMode = OW::Config::ClampFovMode(activePreset.fovMode);
                if (activePreset.fovMode == OW::Config::kFovModeFixed) {
                    activePreset.dynamicFovPresetId = -1;
                } else if (!OW::Config::View_dynamicFovPresets().empty()) {
                    activePreset.dynamicFovPresetId = OW::Config::View_dynamicFovPresets().front().id;
                }
                presetChanged = true;
            }
            ImGui::PopItemWidth();

            if (activePreset.fovMode == OW::Config::kFovModeDynamicPreset) {
                SettingRow("FOV Preset", kAimbotLeftLabelWidth);
                PushControlWidth();
                presetChanged |= UISelectDynamicFovPreset("##aimDynamicFovPreset",
                                                          &activePreset.dynamicFovPresetId,
                                                          false);
                ImGui::PopItemWidth();
                if (!OW::Config::FindDynamicFovPreset(activePreset.dynamicFovPresetId)) {
                    ImGui::TextColored(ImVec4(1.0f, 0.64f, 0.25f, 1.0f),
                                       "Create a Dynamic FOV preset in Misc first.");
                }

                SettingRow("Fallback FOV", kAimbotLeftLabelWidth);
            } else {
                SettingRow("FOV (deg)", kAimbotLeftLabelWidth);
            }
            PushControlWidth();
            presetChanged |= UISlider("##aimFov", &activePreset.fov,
                                      OW::Config::kMinFovDeg, OW::Config::kMaxFovDeg, Number(0, " deg"));
            ImGui::PopItemWidth();

            // Target Priority
            SettingRow("Target Priority", kAimbotLeftLabelWidth);
            PushControlWidth();
            presetChanged |= UISelect("##aimPriority", &activePreset.priority, kPriority, IM_ARRAYSIZE(kPriority));
            ImGui::PopItemWidth();

            // Target Team
            SettingRow("Target Team", kAimbotLeftLabelWidth);
            PushControlWidth();
            presetChanged |= UISelect("##aimTeam", &activePreset.targetTeam, kTeam, IM_ARRAYSIZE(kTeam));
            ImGui::PopItemWidth();
            // Hitbox Scale
            SettingRow("Hitbox Scale", kAimbotRightLabelWidth);
            PushControlWidth();
            presetChanged |= UISlider("##aimHitbox", &activePreset.hitbox,
                                      OW::Config::kMinHitboxScalePercent,
                                      OW::Config::kMaxHitboxScalePercent, Number(0, " %"));
            ImGui::PopItemWidth();

            // Ignore Invisible Targets
            SettingRow("Ignore Invisible Targets", kAimbotRightLabelWidth);
            presetChanged |= UICheckbox("##aimIgnoreInvis", &activePreset.ignoreInvisible);

            // Trace Condition
            SettingRow("Trace Condition", kAimbotRightLabelWidth);
            PushControlWidth();
            presetChanged |= UISelect("##aimTrace", &activePreset.traceCondition, kTrace, IM_ARRAYSIZE(kTrace));
            ImGui::PopItemWidth();

            // Max Distance
            SettingRow("Max Distance", kAimbotRightLabelWidth);
            PushControlWidth();
            presetChanged |= UISlider("##aimMaxDist", &activePreset.maxDistance, 0.0f, 500.0f, Number(0, " m"));
            ImGui::PopItemWidth();

            // Min Distance
            SettingRow("Min Distance", kAimbotRightLabelWidth);
            PushControlWidth();
            presetChanged |= UISlider("##aimMinDist", &activePreset.minDistance, 0.0f, 500.0f, Number(0, " m"));
            ImGui::PopItemWidth();
        CloseGroupBox();
    });

    ImGui::SetNextItemOpen(state.aimAdvanced, ImGuiCond_Always);
    state.aimAdvanced = ImGui::CollapsingHeader(L("Advanced parameters"));
    if (state.aimAdvanced) {
        UITwoColumns([&]() {
        UIGroupBox("Timing & Prediction");
            // Prediction
            SettingRow("Prediction", kAimbotLeftLabelWidth);
            PushControlWidth();
            if (UISelect("##aimPredictionMode", &activePreset.predictionMode,
                         kPredictionMode, IM_ARRAYSIZE(kPredictionMode))) {
                activePreset.prediction = activePreset.predictionMode == 1;
                presetChanged = true;
            }
            ImGui::PopItemWidth();

            SettingRow("Predict FOV Entry", kAimbotLeftLabelWidth);
            presetChanged |= UICheckbox("##aimPredictFovEntry", &activePreset.predictFovEntry);
            if (activePreset.predictFovEntry) {
                SettingRow("Entry Horizon", kAimbotLeftLabelWidth);
                PushControlWidth();
                presetChanged |= UISlider("##aimFovEntryHorizon", &activePreset.fovEntryPredictionMs, 0.0f, 250.0f, Number(0, " ms"));
                ImGui::PopItemWidth();

                SettingRow("Outside Margin", kAimbotLeftLabelWidth);
                PushControlWidth();
                presetChanged |= UISlider("##aimFovEntryMargin", &activePreset.fovEntryMaxOutsideDeg, 0.0f, 15.0f, Number(1, " deg"));
                ImGui::PopItemWidth();
            }

            if (OW::Config::UsesFlickFireControls(activePreset.aimBehavior)) {
                SettingRow("Shot Clamp", kAimbotLeftLabelWidth);
                PushControlWidth();
                presetChanged |= UISlider("##aimFlickShotClamp", &activePreset.flickShotClampMs, 0.0f, 1000.0f, Number(0, " ms"));
                ImGui::PopItemWidth();

                SettingRow("Post-fire Delay", kAimbotLeftLabelWidth);
                PushControlWidth();
                presetChanged |= UISlider("##aimFlickPostFireDelay", &activePreset.flickPostFireDelayMs, 0.0f, 500.0f, Number(0, " ms"));
                ImGui::PopItemWidth();

                if (OW::Config::IsMagneticTriggerBehavior(activePreset.aimBehavior)) {
                    SettingRow("Repeat Interval", kAimbotLeftLabelWidth);
                    PushControlWidth();
                    presetChanged |= UISlider("##aimMagneticInterval", &activePreset.magneticShotIntervalMs, 0.0f, 5000.0f, AtOrBelow("Auto", 0, Number(0, " ms")));
                    ImGui::PopItemWidth();
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", L("0 uses trigger_cycle_interval_ms for the current WeaponSpec. A non-zero value overrides the table."));

                    SettingRow("Aim Pause", kAimbotLeftLabelWidth);
                    PushControlWidth();
                    presetChanged |= UISlider("##aimMagneticPause", &activePreset.magneticPostFirePauseMs, 0.0f, 500.0f, Number(0, " ms"));
                    ImGui::PopItemWidth();

                    SettingRow("Aim Recovery", kAimbotLeftLabelWidth);
                    PushControlWidth();
                    presetChanged |= UISlider("##aimMagneticRecovery", &activePreset.magneticRecoveryMs, 0.0f, 1000.0f, Number(0, " ms"));
                    ImGui::PopItemWidth();

                    SettingRow("Pause X Scale", kAimbotLeftLabelWidth);
                    PushControlWidth();
                    presetChanged |= UISlider("##aimMagneticYawScale", &activePreset.magneticPostFireYawScale, 0.0f, 1.0f, Number(2, "x"));
                    ImGui::PopItemWidth();

                    SettingRow("Pause Y Scale", kAimbotLeftLabelWidth);
                    PushControlWidth();
                    presetChanged |= UISlider("##aimMagneticPitchScale", &activePreset.magneticPostFirePitchScale, 0.0f, 1.0f, Number(2, "x"));
                    ImGui::PopItemWidth();

                    SettingRow("Pre-fire Window", kAimbotLeftLabelWidth);
                    PushControlWidth();
                    presetChanged |= UISlider("##aimMagneticBoostWindow", &activePreset.magneticPreFireBoostWindowMs, 0.0f, 250.0f, Number(0, " ms"));
                    ImGui::PopItemWidth();

                    SettingRow("Pre-fire Boost", kAimbotLeftLabelWidth);
                    PushControlWidth();
                    presetChanged |= UISlider("##aimMagneticBoostScale", &activePreset.magneticPreFireBoostScale, 1.0f, 3.0f, Number(2, "x"));
                    ImGui::PopItemWidth();
                }

                SettingRow("Trajectory Wait", kAimbotLeftLabelWidth);
                presetChanged |= UICheckbox("##aimFlickTrajectoryWait", &activePreset.flickTrajectoryWait);

                if (activePreset.flickTrajectoryWait) {
                    SettingRow("Wait Limit", kAimbotLeftLabelWidth);
                    PushControlWidth();
                    presetChanged |= UISlider("##aimFlickTrajectoryWaitMs", &activePreset.flickTrajectoryWaitMs, 0.0f, 1000.0f, Number(0, " ms"));
                    ImGui::PopItemWidth();

                    SettingRow("Apex Window", kAimbotLeftLabelWidth);
                    PushControlWidth();
                    presetChanged |= UISlider("##aimFlickTrajectoryApexWindow", &activePreset.flickTrajectoryApexWindowMs, 0.0f, 300.0f, Number(0, " ms"));
                    ImGui::PopItemWidth();
                }
            }

            const bool showAimChargeSettings =
                activePreset.firePolicy == static_cast<int>(OW::FirePolicyType::ChargeRelease);
            if (showAimChargeSettings) {
                // Aim Min Charge
                SettingRow("Aim Min Charge", kAimbotRightLabelWidth);
                PushControlWidth();
                presetChanged |= UISlider("##aimMinChg", &activePreset.minCharge, 0.0f, 100.0f, Number(0, " %"));
                ImGui::PopItemWidth();

                // Autoshot Max Charge
                SettingRow("Autoshot Max Charge", kAimbotRightLabelWidth);
                PushControlWidth();
                presetChanged |= UISlider("##aimMaxChg", &activePreset.maxCharge, 0.0f, 100.0f, Number(0, " %"));
                ImGui::PopItemWidth();
            }

        CloseGroupBox();
        }, [&]() {
        UIGroupBox("Tracking & Limits");
            // Max Head Distance
            SettingRow("Max Head Distance", kAimbotLeftLabelWidth);
            PushControlWidth();
            presetChanged |= UISlider("##aimMaxHead", &activePreset.maxHeadDistance, 0.0f, 500.0f, Number(0, " m"));
            ImGui::PopItemWidth();

            // Retarget hysteresis
            SettingRow("Retarget Hysteresis", kAimbotLeftLabelWidth);
            PushControlWidth();
            presetChanged |= UISlider("##aimStick", &activePreset.stickiness, 0.0f, 100.0f, AtOrAbove("Max", 100, Number(1, " %")));
            ImGui::PopItemWidth();

            // Max Aim Time
            SettingRow("Max Aim Time", kAimbotRightLabelWidth);
            PushControlWidth();
            presetChanged |= UISlider("##aimMaxAim", &activePreset.maxAimTime, 0.0f, 100.0f, AtOrAbove("Endless", 100, AtOrBelow("Endless", 0, Number(0, " ms", 20))));
            ImGui::PopItemWidth();

            if (OW::Config::IsFlick2ndBehavior(activePreset.aimBehavior)) {
                SettingRow("Second Method", kAimbotRightLabelWidth);
                PushControlWidth();
                presetChanged |= UISelect("##aimFlick2ndInnerMethod", &activePreset.flick2ndInnerMethod,
                                          kAimMethod, IM_ARRAYSIZE(kAimMethod));
                ImGui::PopItemWidth();

                SettingRow("Trigger Gate", kAimbotRightLabelWidth);
                presetChanged |= UICheckbox("##aimFlick2ndTriggerGate", &activePreset.flick2ndTriggerGate);

                SettingRow("Box Padding", kAimbotRightLabelWidth);
                PushControlWidth();
                presetChanged |= UISlider("##aimFlick2ndBoxPadding", &activePreset.flick2ndBoxPadding, 0.0f, 80.0f, Number(0, " px"));
                ImGui::PopItemWidth();

                SettingRow("Inner Radius", kAimbotRightLabelWidth);
                PushControlWidth();
                presetChanged |= UISlider("##aimFlick2ndInnerRadius", &activePreset.flick2ndInnerRadius, 0.0f, 250.0f, Number(0, " px"));
                ImGui::PopItemWidth();

                SettingRow("Inner Smooth", kAimbotRightLabelWidth);
                PushControlWidth();
                presetChanged |= UISlider("##aimFlick2ndInnerSmooth", &activePreset.flick2ndInnerSmoothScale, 0.1f, 1.0f, Number(2, "x"));
                ImGui::PopItemWidth();
            }

            // Unlock Condition
            SettingRow("Unlock Condition", kAimbotRightLabelWidth);
            PushControlWidth();
            presetChanged |= UISelect("##aimUnlock", &activePreset.unlockCondition, kUnlock, IM_ARRAYSIZE(kUnlock));
            ImGui::PopItemWidth();

            // Lock Time
            SettingRow("Lock Time", kAimbotRightLabelWidth);
            PushControlWidth();
            presetChanged |= UISlider("##aimLockTime", &activePreset.lockTime, 0.0f, 100.0f, Number(0, " ms", 10));
            ImGui::PopItemWidth();

        CloseGroupBox();
        });
    }
    ImGui::Spacing();
    ImGui::TextDisabled("%s", L("Shared presets"));
    if (ImGui::SmallButton(L("Edit behavior preset"))) { state.activeTab = TAB_MISC; state.miscSubTab = 2; }
    ImGui::SameLine();
    if (ImGui::SmallButton(L("Edit method preset"))) { state.activeTab = TAB_MISC; state.miscSubTab = 3; }
    ImGui::SameLine();
    if (ImGui::SmallButton(L("Edit FOV preset"))) { state.activeTab = TAB_MISC; state.miscSubTab = 4; }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", L("Shared presets affect every hero that uses them."));

    if (selectedHero->heroId == 0) {
        if (presetChanged)
            ApplyHeroActionPresetToGlobals(slotKind, activePreset);
    } else {
        if (presetChanged) {
            SetHeroActionPreset(slotKind, selectedHero->heroId, state.aimHeroSegActive, activePreset);
            if (scopeRequirementChanged) {
                OW::Config::SetHeroAimSlotScopeRequirement(
                    selectedHero->heroId,
                    state.aimHeroSegActive,
                    activeScopeRequirement);
            }
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
