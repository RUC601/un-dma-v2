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
#include "ui/AimPlanEditor.hpp"
#include "ui/BehaviorTuningEditor.hpp"
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
    OW::Config::ResolveHeroBehaviorMode(activePreset);

    PushHeroActionSlotControlId(slotKind, selectedHero->heroId, state.aimHeroSegActive);

    slotEnabledChanged |= UIActionSlotEnabledCheckbox(slotKind, *selectedHero, "##aimSlotEnabled", kAimbotHeroLabelWidth);
    ImGui::Spacing();
    UITwoColumns([&]() {
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
        presetChanged |= UI::AimPlanEditor::Policy(activePreset);
        CloseGroupBox();
    }, [&]() {
        UIGroupBox("Behavior & Input");
            SettingRow("Behavior mode",kAimbotLeftLabelWidth);PushControlWidth();
            int chosenMode=activePreset.aimBehaviorPresetId;
            if(UISelectAimBehaviorPreset("##aimBehaviorMode",&chosenMode,-1,"Custom")) {
                OW::Config::StoreHeroBehaviorMode(activePreset);
                activePreset.aimBehaviorPresetId=chosenMode;
                OW::Config::ResolveHeroBehaviorMode(activePreset);presetChanged=true;
            }
            ImGui::PopItemWidth();
            if(chosenMode<0) {
                SettingRow("Aim Behavior",kAimbotLeftLabelWidth);PushControlWidth();
                int behavior=activePreset.aimBehavior;
                if(UISelect("##customBehavior",&behavior,kAimBehavior,IM_ARRAYSIZE(kAimBehavior))) {
                    OW::Config::StoreHeroBehaviorMode(activePreset);activePreset.aimBehavior=behavior;activePreset.resolvedModeId=-2;
                    OW::Config::ResolveHeroBehaviorMode(activePreset);presetChanged=true;
                }
                ImGui::PopItemWidth();
            } else ImGui::TextWrapped("%s: %s + %s",L("Mode structure"),L(kAimBehavior[activePreset.aimBehavior]),L(kAimMethod[activePreset.aimMethod]));

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

            SettingRow("Skeleton", kAimbotLeftLabelWidth);
            PushControlWidth();
            presetChanged |= UISkeletonBoneMultiSelect("##aimSkeleton", &activePreset.aimBoneMask);
            ImGui::PopItemWidth();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", L("Choose All or combine groups, for example Head + Feet."));

        CloseGroupBox();
    });

    UIGroupBox("Current Slot parameters");
    ImGui::TextWrapped("%s",L("These values belong to this Slot. Mode edits change behavior and method, not these values."));
    if(ImGui::Button(L("Restore mode defaults"))) {OW::Config::ResolveHeroBehaviorMode(activePreset,true);presetChanged=true;}
    presetChanged|=UI::BehaviorTuningEditor(activePreset,activePreset.aimBehavior);
    CloseGroupBox();
    if(activePreset.aimBehavior==OW::Config::kAimBehaviorFlick2nd && activePreset.flick2ndInnerMethod>=0 &&
       activePreset.flick2ndInnerMethod!=activePreset.aimMethod) {
        auto& mode=OW::Config::EnsureSlotBehaviorConfig(activePreset,activePreset.aimBehaviorPresetId);
        auto motion=std::find_if(mode.motions.begin(),mode.motions.end(),[&](const auto& m){return m.method==activePreset.flick2ndInnerMethod;});
        if(motion==mode.motions.end()) {
            auto defaults=activePreset.aimPlan.motion;
            if(const auto* definition=OW::Config::FindAimBehaviorPreset(activePreset.aimBehaviorPresetId)) defaults=definition->motion;
            defaults.method=activePreset.flick2ndInnerMethod;mode.motions.push_back(defaults);motion=std::prev(mode.motions.end());
            presetChanged=true;
        }
        UIGroupBox("Second stage motion parameters");
        presetChanged|=UI::AimPlanEditor::Motion(*motion,false);
        CloseGroupBox();
    }
    presetChanged |= UI::AimPlanEditor::Editor(activePreset);

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
        ImGui::SetTooltip("%s", L("Mode structure is shared. Slot parameters stay independent."));

    if(presetChanged) OW::Config::StoreHeroBehaviorMode(activePreset);
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
