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
            ? MakeCurrentHeroActionPreset(slotKind)
            : GetHeroActionPresetOrDefault(slotKind, selectedHero->heroId, state.triggerHeroSegActive);
    };
    refreshActivePreset();

    PushHeroActionSlotControlId(slotKind, selectedHero->heroId, state.triggerHeroSegActive);

    slotEnabledChanged |= UIActionSlotEnabledCheckbox(slotKind, *selectedHero, "##triggerSlotEnabled", kAimbotHeroLabelWidth);
    ImGui::Spacing();

    UITwoColumns([&]() {
        UIGroupBox("Trigger Slot");
        {
            SettingRow("Enable Trigger", kAimbotLeftLabelWidth);
            presetChanged |= UICheckbox("##triggerSlotEnable", &activePreset.trigger.enabled);

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

            SettingRow("Ignore Invisible Targets", kAimbotRightLabelWidth);
            presetChanged |= UICheckbox("##triggerSlotIgnoreInvis", &activePreset.trigger.ignoreInvisible);

        }
        CloseGroupBox();
    });

    UIGroupBox("Target preference");
    presetChanged |= UI::AimPlanEditor::Policy(activePreset, false);
    CloseGroupBox();

    ImGui::SetNextItemOpen(state.triggerAdvanced, ImGuiCond_Always);
    state.triggerAdvanced = ImGui::CollapsingHeader(L("Advanced parameters"));
    if (state.triggerAdvanced) {
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
    }

    if (selectedHero->heroId == 0) {
        if (presetChanged)
            ApplyHeroActionPresetToGlobals(slotKind, activePreset);
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
