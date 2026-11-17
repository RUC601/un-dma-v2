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
                } else if (!OW::Config::Read_dynamicFovPresets().empty()) {
                    activePreset.dynamicFovPresetId = OW::Config::Read_dynamicFovPresets().front().id;
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
            SettingRow(L("Ally key"), kAimbotRightLabelWidth);
            PushControlWidth();
            presetChanged |= UISelect("##allyKey", &activePreset.allyKey, OW::Labels::kAimActivationKeys, OW::Labels::AimActivationKeyCount());
            ImGui::PopItemWidth();
            SettingRow(L("Ally HP limit"), kAimbotRightLabelWidth);
            PushControlWidth();
            presetChanged |= UISlider("##allyHp", &activePreset.allyHealthMaxPercent,0.f,100.f,Number(0," %"));
            ImGui::PopItemWidth();
            SettingRow(L("Ally skeleton"), kAimbotRightLabelWidth);
            PushControlWidth();
            presetChanged |= UISkeletonBoneMultiSelect("##allyBones",&activePreset.allyBoneMask);
            ImGui::PopItemWidth();
            SettingRow(L("Ignore Sombra stealth"), kAimbotRightLabelWidth);
            presetChanged |= UICheckbox("##stealthFilter",&activePreset.ignoreSombraStealth);
            SettingRow(L("Protection filter"), kAimbotRightLabelWidth);
            presetChanged |= UICheckbox("##targetableFilter",&activePreset.requireTargetable);
            if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",L("Existing protection signals; full targetability pending validation."));
            // Hitbox Scale
            SettingRow("Hitbox Scale", kAimbotRightLabelWidth);
            PushControlWidth();
            presetChanged |= UISlider("##aimHitbox", &activePreset.hitbox,
                                      OW::Config::kMinHitboxScalePercent,
                                      OW::Config::kMaxHitboxScalePercent, Number(0, " %"));
            ImGui::PopItemWidth();

            // Visibility is the user-facing form of the legacy ignoreInvisible bool.
            SettingRow(L("Visibility"), kAimbotRightLabelWidth);
            PushControlWidth();
            static const char* const kVisibility[] = { "Visible Only", "All" };
            int visibility = activePreset.ignoreInvisible ? 0 : 1;
            if (UISelect("##aimVisibility", &visibility, kVisibility, IM_ARRAYSIZE(kVisibility))) {
                activePreset.ignoreInvisible = visibility == 0;
                presetChanged = true;
            }
            ImGui::PopItemWidth();

            const bool contourSupported = activePreset.aimBehavior == OW::Config::kAimBehaviorFlick ||
                activePreset.aimBehavior == OW::Config::kAimBehaviorMagneticTrigger;
            SettingRow(OW::Config::uiLanguage == 1 ? "轮廓确认" : "Contour confirmation", kAimbotRightLabelWidth);
            ImGui::BeginDisabled(!contourSupported && !activePreset.contourEnabled);
            presetChanged |= UICheckbox("##aimContour", &activePreset.contourEnabled);
            ImGui::EndDisabled();
            if (activePreset.contourEnabled || !contourSupported) {
                ImGui::TextWrapped("%s", OW::Config::uiLanguage == 1
                    ? (contourSupported ? "原生判定可见的目标继续走 UN 算法；仅对原生判定不可见的目标用采集画面确认露出部位。三种状态颜色均参与匹配，请在设置 → 屏幕开启采集。"
                                        : "轮廓确认仅支持 Flick 和磁性扳机。切换 Behavior 后若保留开启，该 Slot 不会对不可见目标补充确认；可在此关闭。")
                    : (contourSupported ? "Targets accepted by native visibility keep the UN path. Captured contours are used only to supplement targets native visibility marked as hidden. Start capture under Settings > Screen."
                                        : "Contour confirmation supports Flick and Magnetic Trigger. It does not supplement hidden targets for another behavior; turn it off here if needed."));
            }
            if (activePreset.contourEnabled) {
                V2::Vision::Settings captureSettings;
                const auto captureFrame = V2::Vision::GetService().Snapshot(captureSettings);
                const auto captureStatus = V2::Vision::GetService().Status();
                const bool zh = OW::Config::uiLanguage == 1;
                const char* captureMessage = nullptr;
                if (!captureSettings.enabled || captureStatus.state == V2::Vision::ServiceState::Stopped)
                    captureMessage = zh ? "轮廓采集未开启：当前 Slot 等待画面，不会瞄准或开火。请先开始采集。"
                                        : "Capture is stopped. This Slot waits for an image and cannot aim or fire. Start capture first.";
                else if (captureStatus.state == V2::Vision::ServiceState::Faulted)
                    captureMessage = zh ? "轮廓采集失败：当前 Slot 已暂停，请到采集设置查看原因。"
                                        : "Capture failed. This Slot is paused; check the capture settings for details.";
                else if (captureStatus.state == V2::Vision::ServiceState::Starting)
                    captureMessage = zh ? "轮廓采集正在连接：当前 Slot 等待新画面。"
                                        : "Capture is starting. This Slot is waiting for a new image.";
                else if (!captureFrame)
                    captureMessage = zh ? "轮廓采集没有新鲜画面：当前 Slot 等待画面恢复。"
                                        : "No fresh capture frame. This Slot is waiting for the image to recover.";
                if (captureMessage) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.64f, 0.25f, 1.0f));
                    ImGui::TextWrapped("%s", captureMessage);
                    ImGui::PopStyleColor();
                } else {
                    ImGui::TextWrapped("%s", zh ? "已收到新鲜采集画面；目标部位仍须通过轮廓关联与触边确认。"
                                                : "Fresh capture frames are available. The target part still requires contour association and contact confirmation.");
                }
                if (ImGui::SmallButton(zh ? "打开采集设置" : "Open capture settings")) {
                    state.activeTab = TAB_MISC;
                    state.miscSubTab = 6;
                }
            }

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
            if(UISelectAimBehaviorPreset("##aimBehaviorMode",&chosenMode,-1,nullptr)) {
                OW::Config::StoreHeroBehaviorMode(activePreset);
                activePreset.aimBehaviorPresetId=chosenMode;
                OW::Config::ResolveHeroBehaviorMode(activePreset);presetChanged=true;
            }
            ImGui::PopItemWidth();
            if(ImGui::IsItemHovered())ImGui::SetTooltip("%s: %s + %s",L("Mode structure"),L(kAimBehavior[activePreset.aimBehavior]),L(kAimMethod[activePreset.aimMethod]));

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

            if (activePreset.aimBehavior == OW::Config::kAimBehaviorFlick ||
                activePreset.aimBehavior == OW::Config::kAimBehaviorMagneticTrigger) {
                SettingRow(OW::Config::uiLanguage == 1 ? "开火模式" : "Firing mode", kAimbotLeftLabelWidth);
                PushControlWidth();
                const char* const firingModes[] = {
                    OW::Config::uiLanguage == 1 ? "持续开火" : "Continuous fire",
                    OW::Config::uiLanguage == 1 ? "单发" : "Single shot"
                };
                int firingMode = activePreset.keepFiring ? 0 : 1;
                if (UISelect("##aimFiringMode", &firingMode, firingModes, IM_ARRAYSIZE(firingModes))) {
                    activePreset.firePolicy = static_cast<int>(
                        firingMode == 0 ? OW::FirePolicyType::HoldWhileTracking
                                        : OW::FirePolicyType::TapOnHitWindow);
                    activePreset.keepFiring = firingMode == 0;
                    activePreset.autoshot = firingMode != 0;
                    presetChanged = true;
                }
                ImGui::PopItemWidth();
                ImGui::TextWrapped("%s", OW::Config::uiLanguage == 1
                    ? "连续开火会在当前激活期间按节奏重复开火；单发命中后结束本次开火。"
                    : "Continuous fire repeats during the active slot; single shot stops after one hit-window shot.");
            }

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
    SettingRow("Base speed");PushControlWidth();
    presetChanged|=UISlider("##slotBaseSpeed",&activePreset.baseSpeed,0,100,Number(1," %"));ImGui::PopItemWidth();
    if(ImGui::Button(L("Edit motion...")))ImGui::OpenPopup("##MotionEditor");
    ImGui::SameLine();
    if(ImGui::Button(L("Edit target hero exceptions...")))ImGui::OpenPopup("##ExceptionEditor");
    CloseGroupBox();
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
