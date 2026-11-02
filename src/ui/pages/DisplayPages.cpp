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

void UI::VisualsPage() {
    // Player Visual Features -- renderer-backed OW::Config toggles.
    UIGroupBox("Player Visual Features");
    {
        const char* labels[] = {
            "Box", "Hero Name", "BattleTag",
            "Health Color", "Health Bar", "Health Bar 2",
            "Distance", "Ultimate", "Skill Info",
            "Skeleton", "Hero Avatar", "Model Shapes (research)",
            "Snapline", "Eye Ray", "Radar",
            "Radar Lines", "FOV Circle", "Tracking Deadzone",
            "Crosshair", "Health Packs", nullptr
        };
        bool* values[] = {
            &OW::Config::draw_info, &OW::Config::name, &OW::Config::drawbattletag,
            &OW::Config::drawhealth, &OW::Config::healthbar, &OW::Config::healthbar2,
            &OW::Config::dist, &OW::Config::ult, &OW::Config::skillinfo,
            &OW::Config::draw_skel, &OW::Config::draw_avatar, &OW::Config::draw_hitbox,
            &OW::Config::drawline, &OW::Config::eyeray, &OW::Config::radar,
            &OW::Config::radarline, &OW::Config::draw_fov, &OW::Config::drawTrackingDeadzones,
            &OW::Config::crosscircle, &OW::Config::draw_hp_pack, nullptr
        };
        const float ratios[] = { 1.0f, 1.0f, 1.2f };
        DrawCheckboxGrid3(labels, values, 7, 26.0f, ratios);
        if(OW::Config::draw_hitbox)
            ImGui::TextWrapped("%s",(OW::Config::uiLanguage==OW::Config::kUiLanguageChineseSimplified ?
                "已匹配模型使用原始形状；绑定/姿态及伤害用途待核。未匹配角色沿用估计几何。" :
                "Matched models use extracted shapes; pose and damage use unverified. Unmatched models use estimated geometry."));
    }
    CloseGroupBox();

    // Filters
    UIGroupBox("Filters");
    {
        SettingRow("Max Distance");
        PushControlWidth();
        UISlider("##visMaxDist", &OW::Config::visualMaxDist, 0.0f, 1000.0f, Number(0, " m"));
        ImGui::PopItemWidth();
    }
    CloseGroupBox();
}

// =====================================================================
// UI::ThemePage
// =====================================================================
void DrawThemeGeneralPage() {
    UIGroupBox("ESP Display Position");
    {
        static const char* kDisplayPosition[] = { "Above Head", "Left Side", "Right Side" };
        static const char* kUltimateRosterFilter[] = { "All", "Enemy", "Ally" };
        static const char* kRadarCorner[] = { "Bottom Right", "Bottom Left", "Top Right", "Top Left" };

        auto drawPositionSelect = [](const char* rowLabel, const char* controlId,
                                     bool visualEnabled, int* value,
                                     const char* const items[], int itemCount) {
            SettingRow(rowLabel);
            PushControlWidth();
            if (visualEnabled) {
                UISelect(controlId, value, items, itemCount);
            } else {
                UIDisabledSelect(controlId);
            }
            ImGui::PopItemWidth();
        };

        drawPositionSelect("Radar Corner", "##radarCorner", OW::Config::radar,
                           &OW::Config::radarCorner, kRadarCorner, IM_ARRAYSIZE(kRadarCorner));
        drawPositionSelect("Skill Cooldowns", "##skillDisplayMode", OW::Config::skillinfo,
                           &OW::Config::skillDisplayMode, kDisplayPosition, IM_ARRAYSIZE(kDisplayPosition));
        drawPositionSelect("Ultimate Roster", "##ultimateRosterFilter", OW::Config::ult,
                           &OW::Config::ultimateRosterFilter,
                           kUltimateRosterFilter, IM_ARRAYSIZE(kUltimateRosterFilter));
    }
    CloseGroupBox();

    UIGroupBox("Overlay Colors");
    {
        UIColorEdit("Box Outline", &OW::Config::EnemyCol);
        UIColorEdit("FOV Circle", &OW::Config::fovcol);
        UIColorEdit("Visible Fill", &OW::Config::enargb);
        UIColorEdit("Hidden Fill", &OW::Config::invisnenargb);
        UIColorEdit("Target Fill", &OW::Config::targetargb);
        UIColorEdit("Ally Fill", &OW::Config::allyargb);
    }
    CloseGroupBox();
}

static bool UIFovRingCompactColorEdit(const char* label, ImVec4* value) {
    ImGui::SetNextItemWidth(92.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, kColControl);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, kColControlHover);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, kColControlHot);
    ImGui::PushStyleColor(ImGuiCol_Border, kColStroke);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, kControlRounding);
    const bool changed = ImGui::ColorEdit4(
        label,
        &value->x,
        ImGuiColorEditFlags_NoLabel |
        ImGuiColorEditFlags_NoInputs |
        ImGuiColorEditFlags_AlphaBar |
        ImGuiColorEditFlags_DisplayRGB |
        ImGuiColorEditFlags_InputRGB);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    return changed;
}

static bool UIFovRingStyleRow(OW::Config::FovRingSlotKind kind,
                              int slotIndex,
                              bool slotEnabled,
                              float slotFovDeg) {
    OW::Config::FovRingSlotStyle& style = OW::Config::FovRingStyleFor(kind, slotIndex);
    style = OW::Config::ClampFovRingStyle(style, kind, slotIndex);

    bool changed = false;
    ImGui::PushID(static_cast<int>(kind) * 100 + slotIndex);

    char slotLabel[32] = {};
    std::snprintf(slotLabel, sizeof(slotLabel), L("Slot %d"), slotIndex + 1);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(slotLabel);

    ImGui::SameLine(74.0f);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(slotEnabled ? kColTextMuted : kColTextDim),
                       "%s %.0f deg",
                       slotEnabled ? L("On") : L("Off"),
                       OW::Config::ClampFovDeg(slotFovDeg));

    ImGui::SameLine(160.0f);
    changed |= UICheckbox("##visible", &style.visible);
    ImGui::SameLine(181.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(L("Show"));

    ImGui::SameLine(232.0f);
    changed |= UIFovRingCompactColorEdit("##color", &style.color);

    ImGui::SameLine(334.0f);
    ImGui::SetNextItemWidth(102.0f);
    changed |= UISlider("##thickness", &style.thickness, 0.5f, 6.0f, Number(1, " px"));

    ImGui::SameLine(448.0f);
    static const char* kLineStyles[] = { "Solid", "Dashed" };
    ImGui::SetNextItemWidth(88.0f);
    changed |= UISelect("##lineStyle", &style.lineStyle, kLineStyles, IM_ARRAYSIZE(kLineStyles));

    ImGui::SameLine(548.0f);
    changed |= UICheckbox("##label", &style.showLabel);
    ImGui::SameLine(569.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(L("Label"));

    if (changed)
        style = OW::Config::ClampFovRingStyle(style, kind, slotIndex);

    ImGui::PopID();
    return changed;
}

static void DrawFovRingStyleGroup(OW::Config::FovRingSlotKind kind,
                                  const HeroOption& selectedHero) {
    UIGroupBox("Aim Slot Rings");
    {
        const int slotCount = selectedHero.heroId == 0
            ? OW::Config::kMaxHeroPresetSlots
            : ImClamp(OW::Config::GetHeroAimSlotCount(selectedHero.heroId),
                1,
                OW::Config::kMaxHeroPresetSlots);

        for (int slotIndex = 0; slotIndex < slotCount; ++slotIndex) {
            OW::Config::HeroSlotPreset slot{};
            const bool hasSlot = selectedHero.heroId != 0 &&
                OW::Config::TryGetHeroAimSlot(selectedHero.heroId, slotIndex, slot);
            const bool slotEnabled = hasSlot && slot.enabled;
            const float fovDeg = hasSlot ? slot.preset.fov : OW::Config::kDefaultFovDeg;

            UIFovRingStyleRow(kind, slotIndex, selectedHero.heroId == 0 || slotEnabled, fovDeg);
        }
    }
    CloseGroupBox();
}

void DrawThemeFovRingsPage() {
    const HeroOption& selectedHero = CurrentHeroOption();
    DrawFovRingStyleGroup(OW::Config::FovRingSlotKind::Aim, selectedHero);
}

void UI::ThemePage() {
    ImGui::PushID("ThemePage");

    state.themeSubTab = ImClamp(state.themeSubTab, 0, kThemeSubTabCount - 1);
    if (state.themeSubTab == 0)
        DrawThemeGeneralPage();
    else
        DrawThemeFovRingsPage();

    ImGui::PopID();
}

// =====================================================================
// UI::MiscPage
// =====================================================================
