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
using namespace UI::Theme;

void UI::TriggerPage() {
    state.selectedTypeIndex = ClampHeroSelectionIndex(state.selectedTypeIndex);
    const HeroOption* selectedHero = &kHeroOptions[state.selectedTypeIndex];
    constexpr ActionSlotKind slotKind = ActionSlotKind::Trigger;

    const int previousSlot = state.triggerHeroSegActive;
    const bool slotSelectorChanged = UIActionSlotSelector(slotKind, selectedHero->heroId);
    state.triggerHeroSegActive = ActiveHeroPresetSlotIndex(slotKind);
    if (slotSelectorChanged || previousSlot != state.triggerHeroSegActive)
        ApplySelectedTypePreset();

    bool presetChanged = false;
    bool slotEnabledChanged = false;
    OW::Config::HeroPreset activePreset{};

    auto refreshActivePreset = [&]() {
        state.selectedTypeIndex = ClampHeroSelectionIndex(state.selectedTypeIndex);
        selectedHero = &kHeroOptions[state.selectedTypeIndex];
        const bool isGlobal = selectedHero->heroId == 0;
        activePreset = isGlobal
            ? OW::Config::GetSystemHeroPreset()
            : GetHeroActionPresetOrDefault(slotKind, selectedHero->heroId, state.triggerHeroSegActive);
    };
    refreshActivePreset();

    PushHeroActionSlotControlId(slotKind, selectedHero->heroId, state.triggerHeroSegActive);

    // One UI switch controls both legacy gates; merely viewing an old slot never changes it.
    bool triggerEnabled = activePreset.trigger.enabled && (selectedHero->heroId == 0 ||
        IsHeroActionSlotEnabled(slotKind, selectedHero->heroId, state.triggerHeroSegActive));
    SettingRow("Enable Trigger", kAimbotHeroLabelWidth);
    if (UICheckbox("##triggerEnabled", &triggerEnabled)) {
        activePreset.trigger.enabled = triggerEnabled;
        if (selectedHero->heroId != 0)
            OW::Config::SetHeroTriggerSlotEnabled(selectedHero->heroId, state.triggerHeroSegActive, triggerEnabled);
        presetChanged = slotEnabledChanged = true;
    }
    ImGui::Spacing();

    UITwoColumns([&]() {
        UIGroupBox("Trigger Slot");
        {
            SettingRow("Action", kAimbotLeftLabelWidth);
            PushControlWidth();
            presetChanged |= UIAttackActionSelect("##triggerSlotAction", &activePreset.trigger.action, selectedHero->heroId);
            ImGui::PopItemWidth();

            SettingRow("Mode", kAimbotLeftLabelWidth);
            PushControlWidth();
            if (UISelect("##triggerSlotMode", &activePreset.trigger.mode,
                         OW::Labels::kTriggerbotModes, OW::Labels::TriggerbotModeCount())) {
                if (activePreset.trigger.mode == 2)
                    activePreset.trigger.key = OW::Labels::kAimActivationKeyNoneIndex;
                presetChanged = true;
            }
            ImGui::PopItemWidth();

            SettingRow("Activation Key", kAimbotLeftLabelWidth);
            PushControlWidth();
            const bool triggerAlways = activePreset.trigger.mode == 2;
            if (triggerAlways) {
                activePreset.trigger.key = OW::Labels::kAimActivationKeyNoneIndex;
                ImGui::BeginDisabled();
            }
            presetChanged |= UISelect("##triggerSlotKey", &activePreset.trigger.key,
                                      OW::Labels::kAimActivationKeys, OW::Labels::AimActivationKeyCount());
            if (triggerAlways)
                ImGui::EndDisabled();
            ImGui::PopItemWidth();
        }
        CloseGroupBox();
    }, [&]() {
        UIGroupBox("Trigger Conditions");
        {
            SettingRow("Shot Interval", kAimbotRightLabelWidth);
            PushControlWidth();
            presetChanged |= UISlider("##triggerSlotInterval", &activePreset.trigger.shotInterval,
                                      0.0f, 100.0f, AtOrBelow("Auto", 0, Number(0, " ms", 5)));
            ImGui::PopItemWidth();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", L("0 uses trigger_cycle_interval_ms for the current WeaponSpec. A non-zero value remains the manual override."));

            SettingRow("Disable During Reload", kAimbotRightLabelWidth);
            presetChanged |= UICheckbox(
                "##triggerSlotDisableWhileReloading",
                &activePreset.trigger.disableWhileReloading);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", L("Skip this Trigger Slot while the local hero is reloading."));

            SettingRow("Skeleton", kAimbotRightLabelWidth);
            PushControlWidth();
            presetChanged |= UISkeletonBoneMultiSelect(
                "##triggerSlotSkeleton",
                &activePreset.trigger.boneMask);
            ImGui::PopItemWidth();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", L("Choose All or combine groups, for example Head + Feet."));

            SettingRow("Target Filter", kAimbotRightLabelWidth);
            PushControlWidth();
            presetChanged |= UISelect("##triggerSlotTeam", &activePreset.targetTeam,
                                      kTeam, IM_ARRAYSIZE(kTeam));
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
            SettingRow("Ignore Invisible Targets", kAimbotRightLabelWidth);
            presetChanged |= UICheckbox("##triggerSlotIgnoreInvis", &activePreset.trigger.ignoreInvisible);

        }
        CloseGroupBox();
    });

    UIGroupBox("Target preference");
    presetChanged |= UI::AimPlanEditor::Policy(activePreset, false);
    CloseGroupBox();

    if(ImGui::Button(L("Edit advanced parameters...")))ImGui::OpenPopup("##TriggerAdvanced");
    const auto viewport=ImGui::GetMainViewport();
    ImGui::SetNextWindowSizeConstraints(ImVec2(300,180),ImVec2(viewport->WorkSize.x-24,viewport->WorkSize.y-24));
    ImGui::SetNextWindowSize(ImVec2((std::min)(650.f,viewport->WorkSize.x-24),380),ImGuiCond_Appearing);
    if (ImGui::BeginPopup("##TriggerAdvanced",ImGuiWindowFlags_NoSavedSettings)) {
        UITwoColumns([&]() {
            UIGroupBox("Timing & Prediction");
            SettingRow("Charge Aware", kAimbotRightLabelWidth);
            presetChanged |= UICheckbox("##triggerSlotChargeAware", &activePreset.trigger.chargeAware);

            if (activePreset.trigger.chargeAware) {
                SettingRow("Min Charge", kAimbotRightLabelWidth);
                PushControlWidth();
                presetChanged |= UISlider("##triggerSlotMinCharge", &activePreset.trigger.minCharge,
                                          0.0f, 100.0f, Number(2, " %"));
                ImGui::PopItemWidth();
            }

            CloseGroupBox();
        }, [&]() {
            UIGroupBox("Range & Target");
            SettingRow("Detection Scale", kAimbotRightLabelWidth);
            PushControlWidth();
            presetChanged |= UISlider("##triggerSlotHitbox", &activePreset.hitbox,
                                      OW::Config::kMinHitboxScalePercent,
                                      OW::Config::kMaxHitboxScalePercent, Number(0, " %"));
            ImGui::PopItemWidth();

            CloseGroupBox();
        });
        ImGui::EndPopup();
    }

    if (selectedHero->heroId == 0) {
        if (presetChanged)
            OW::Config::SetSystemTriggerPreset(activePreset);
    } else {
        if (presetChanged) {
            SetHeroActionPreset(slotKind, selectedHero->heroId, state.triggerHeroSegActive, activePreset);
            ApplyHeroActionPresetToGlobals(slotKind, activePreset);
        } else if (slotEnabledChanged &&
                   IsHeroActionSlotEnabled(slotKind, selectedHero->heroId, state.triggerHeroSegActive)) {
            ApplyHeroActionPresetToGlobals(slotKind, activePreset);
        }
    }

    ImGui::PopID();
}
