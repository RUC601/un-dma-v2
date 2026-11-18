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
};

constexpr const char* kAimFireModes[] = {
    "Keep Firing",
    "Single Shot",
    "Tracking With Hold",
};

AimFireMode AimFireModeFromPolicy(int policy)
{
    switch (OW::ClampFirePolicy(policy)) {
    case OW::FirePolicyType::KeepFiring:
        return AimFireMode::KeepFiring;
    case OW::FirePolicyType::SingleShot:
        return AimFireMode::SingleShot;
    case OW::FirePolicyType::TrackingWithHold:
    default:
        return AimFireMode::TrackingWithHold;
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

std::string AimActivationKeyLabel(int keySetting)
{
    if (!OW::Labels::IsCustomAimActivationKey(keySetting))
        return OW::Labels::AimActivationKeyName(keySetting);

    const int vk = OW::Labels::AimActivationKeyVk(keySetting);
    if (vk == VK_LBUTTON) return "Left Mouse";
    if (vk == VK_RBUTTON) return "Right Mouse";
    if (vk == VK_MBUTTON) return "Middle Mouse";
    if (vk == VK_XBUTTON1) return "Mouse 4";
    if (vk == VK_XBUTTON2) return "Mouse 5";

    wchar_t name[64]{};
    const UINT scan = MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC);
    if (scan && GetKeyNameTextW(static_cast<LONG>(scan) << 16, name, 64)) {
        char utf8[128]{};
        if (WideCharToMultiByte(CP_UTF8, 0, name, -1, utf8,
                                static_cast<int>(sizeof(utf8)), nullptr, nullptr)) {
            return utf8;
        }
    }

    char fallback[32]{};
    std::snprintf(fallback, sizeof(fallback), "VK 0x%02X", vk);
    return fallback;
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

    slotEnabledChanged |= UIActionSlotEnabledCheckbox(slotKind, *selectedHero, "##aimSlotEnabled", kAimbotHeroLabelWidth);
    ImGui::Spacing();
    UITwoColumns([&]() {
        UIGroupBox("Basic Options · Target Eligibility");
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
            SettingRow(L("Protection filter"), kAimbotRightLabelWidth);
            presetChanged |= UICheckbox("##targetableFilter",&activePreset.requireTargetable);
            if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",L("Skips targets with a fresh, explicitly known protection signal."));
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

            const bool leakSupported = activePreset.aimBehavior == OW::Config::kAimBehaviorFlick ||
                activePreset.aimBehavior == OW::Config::kAimBehaviorMagneticTrigger;
            SettingRow(L("Leak"), kAimbotRightLabelWidth);
            ImGui::BeginDisabled(!leakSupported && !activePreset.contourEnabled);
            presetChanged |= UICheckbox("##aimContour", &activePreset.contourEnabled);
            ImGui::EndDisabled();
            if (activePreset.contourEnabled || !leakSupported) {
                ImGui::TextWrapped("%s", OW::Config::uiLanguage == 1
                    ? (leakSupported ? "原生判定可见的目标继续走 UN 算法；仅对原生判定不可见的目标用采集画面确认露出部位。三种状态颜色均参与匹配，请在设置 → 屏幕开启采集。"
                                        : "漏打仅支持 Flick 和磁性扳机。切换 Behavior 后若保留开启，该 Slot 不会对不可见目标补充确认；可在此关闭。")
                    : (leakSupported ? "Targets accepted by native visibility keep the UN path. Captured leak evidence are used only to supplement targets native visibility marked as hidden. Start capture under Settings > Screen."
                                        : "Leak supports Flick and Magnetic Trigger. It does not supplement hidden targets for another behavior; turn it off here if needed."));
            }
            if (activePreset.contourEnabled) {
                V2::Vision::Settings captureSettings;
                const auto captureFrame = V2::Vision::GetService().Snapshot(captureSettings);
                const auto captureStatus = V2::Vision::GetService().Status();
                const bool zh = OW::Config::uiLanguage == 1;
                const char* captureMessage = nullptr;
                if (!captureSettings.enabled || captureStatus.state == V2::Vision::ServiceState::Stopped)
                    captureMessage = zh ? "漏打采集未开启：当前 Slot 等待画面，不会瞄准或开火。请先开始采集。"
                                        : "Capture is stopped. This Slot waits for an image and cannot aim or fire. Start capture first.";
                else if (captureStatus.state == V2::Vision::ServiceState::Faulted)
                    captureMessage = zh ? "漏打采集失败：当前 Slot 已暂停，请到采集设置查看原因。"
                                        : "Capture failed. This Slot is paused; check the capture settings for details.";
                else if (captureStatus.state == V2::Vision::ServiceState::Starting)
                    captureMessage = zh ? "漏打采集正在连接：当前 Slot 等待新画面。"
                                        : "Capture is starting. This Slot is waiting for a new image.";
                else if (!captureFrame)
                    captureMessage = zh ? "漏打采集没有新鲜画面：当前 Slot 等待画面恢复。"
                                        : "No fresh capture frame. This Slot is waiting for the image to recover.";
                if (captureMessage) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.64f, 0.25f, 1.0f));
                    ImGui::TextWrapped("%s", captureMessage);
                    ImGui::PopStyleColor();
                } else {
                    ImGui::TextWrapped("%s", zh ? "已收到新鲜采集画面；漏打结果提供目标部位和瞄准方向，开火继续使用当前 Behavior。"
                                                : "Fresh capture frames are available. Leak recognition supplies the aim direction, while firing follows the current Behavior.");
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

            // Aim activation key is stored per Aim Slot.  The legacy catalog
            // remains available through the capture button, while captured
            // keys use a stable encoded VK value in the same integer field.
            SettingRow("Aim Activation Key", kAimbotLeftLabelWidth);
            PushControlWidth();
            static uint64_t captureHero = 0;
            static int captureSlot = -1;
            static bool captureArmed = false;
            const bool capturing = captureHero == selectedHero->heroId &&
                captureSlot == state.aimHeroSegActive;
            const std::string keyLabel = capturing
                ? std::string(L("Press a key... Esc cancels"))
                : AimActivationKeyLabel(activePreset.key);
            if (ImGui::Button(keyLabel.c_str(), ImVec2(220.0f, ControlHeight()))) {
                captureHero = selectedHero->heroId;
                captureSlot = state.aimHeroSegActive;
                captureArmed = false;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(L("Clear"))) {
                activePreset.key = OW::Labels::kAimActivationKeyNoneIndex;
                presetChanged = true;
                captureHero = 0;
                captureSlot = -1;
            }
            if (capturing) {
                if (!captureArmed) {
                    const bool mouseDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 ||
                        (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0 ||
                        (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0 ||
                        (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) != 0 ||
                        (GetAsyncKeyState(VK_XBUTTON2) & 0x8000) != 0;
                    if (!mouseDown) captureArmed = true;
                } else if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
                    captureHero = 0;
                    captureSlot = -1;
                    captureArmed = false;
                } else {
                    for (int vk = 1; vk < 256; ++vk) {
                        if (!V2::HeroSettings::SupportedKey(vk) ||
                            !(GetAsyncKeyState(vk) & 0x8000))
                            continue;
                        activePreset.key = OW::Labels::AimActivationKeyIndexFromVk(vk);
                        presetChanged = true;
                        captureHero = 0;
                        captureSlot = -1;
                        captureArmed = false;
                        break;
                    }
                }
            }
            ImGui::PopItemWidth();

            if (OW::Config::HeroUsesScopedWeaponActionSplit(selectedHero->heroId)) {
            // Scope Requirement
            SettingRow("Scope Requirement", kAimbotLeftLabelWidth);
            ImGui::TextDisabled("%s", activeScopeRequirement == OW::Config::AimScopeRequirement::ScopedOnly
                ? L("Scoped Only (from Action)")
                : L("All (from Action)"));
            ImGui::SetItemTooltip("%s", L("The selected attack action determines the required stance."));

            }

            // Attack
            SettingRow("Attack", kAimbotLeftLabelWidth);
            PushControlWidth();
            if (UIAttackActionSelect("##aimAttack", &activePreset.trigger.action, selectedHero->heroId)) {
                activeScopeRequirement =
                    OW::Config::DefaultAimScopeRequirementForHeroAction(
                        selectedHero->heroId,
                        activePreset.trigger.action);
                scopeRequirementChanged = true;
                presetChanged = true;
            }
            ImGui::PopItemWidth();

            SettingRow(OW::Config::uiLanguage == 1 ? "开火模式" : "Fire behavior", kAimbotLeftLabelWidth);
            PushControlWidth();
            AimFireMode fireMode = AimFireModeFromPolicy(activePreset.firePolicy);
            int fireModeIndex = static_cast<int>(fireMode);
            if (UISelect("##aimFireMode", &fireModeIndex,
                         kAimFireModes, IM_ARRAYSIZE(kAimFireModes))) {
                fireMode = static_cast<AimFireMode>(std::clamp(fireModeIndex, 0, 2));
                activePreset.firePolicy = AimFirePolicyFromMode(fireMode);
                activePreset.keepFiring = fireMode != AimFireMode::SingleShot;
                activePreset.autoshot = fireMode != AimFireMode::KeepFiring;
                // The old field represented a physical-action gate. The new
                // fire modes own their activation cycle and no longer expose
                // that gate as a second user-facing control.
                activePreset.requireActionHeld = false;
                presetChanged = true;
            }
            ImGui::PopItemWidth();
            ImGui::TextWrapped("%s", OW::Config::uiLanguage == 1
                ? "持续按住会在激活键按下期间持有攻击键；单发在本轮只提交一次并等待松开重按；跟踪并持续按住只在跟踪会话中持有攻击键。"
                : "Keep Firing owns the attack button while the activation key is down; Single Shot submits once per press cycle; Tracking With Hold owns it only during a tracking session.");

            SettingRow("Skeleton", kAimbotLeftLabelWidth);
            PushControlWidth();
            presetChanged |= UISkeletonBoneMultiSelect("##aimSkeleton", &activePreset.aimBoneMask);
            ImGui::PopItemWidth();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", L("Choose All or combine groups, for example Head + Feet."));

        CloseGroupBox();
    });

    UIGroupBox("Advanced Options · Behavior Tuning");
    SettingRow("Global response scale");PushControlWidth();
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
        UIGroupBox("Advanced Options · FOV & Prediction Entry");
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

            SettingRow("Target Priority", kAimbotLeftLabelWidth);
            PushControlWidth();
            presetChanged |= UISelect("##aimPriority", &activePreset.priority, kPriority, IM_ARRAYSIZE(kPriority));
            ImGui::PopItemWidth();

            SettingRow("Prediction", kAimbotLeftLabelWidth);
            ImGui::TextDisabled("Automatic");
            ImGui::SetItemTooltip("%s", L("Prediction stays enabled. Edit shared weapon and skill data under Prediction Data."));

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
