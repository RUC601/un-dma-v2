#include <Windows.h>
#include <shellapi.h>

#include "Features/UI.hpp"
#include "ui/WorkspaceShell.hpp"
#include "ui/SlotSelector.hpp"
#include "ui/Text.hpp"
#include "ui/Theme.hpp"
#include "ui/ColorPicker.hpp"
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
using namespace UI::Theme;

static bool DrawHeroSkillSequenceSteps(const OW::HeroSkillDefinition& definition,
                                       OW::Config::HeroSkillSettings& settings) {
    bool changed = false;

    ImGui::Spacing();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kColTextMuted), "%s", L("Sequence Steps"));

    if (!settings.sequenceSteps.empty()) {
        float speedScale = settings.sequenceSteps.front().speedScale;
        SettingRow("Speed Scale", kAimbotRightLabelWidth);
        PushControlWidth();
        if (UISlider("##sequenceSpeedScale", &speedScale, 0.5f, 2.0f, Number(2, "x"))) {
            for (OW::Config::HeroSkillSequenceStep& step : settings.sequenceSteps)
                step.speedScale = speedScale;
            changed = true;
        }
        ImGui::PopItemWidth();
    }

    for (int index = 0; index < static_cast<int>(settings.sequenceSteps.size()); ++index) {
        OW::Config::HeroSkillSequenceStep& step = settings.sequenceSteps[static_cast<size_t>(index)];
        ImGui::PushID(index);

        ImGui::AlignTextToFramePadding();
        ImGui::Text("#%d", index + 1);
        ImGui::SameLine(0.0f, 8.0f);

        bool left = (step.buttonMask & 1) != 0;
        bool right = (step.buttonMask & 2) != 0;
        if (UICheckbox(L("Primary"), &left)) { step.buttonMask ^= 1; changed = true; }
        ImGui::SameLine(0.0f, 4.0f);
        if (UICheckbox(L("Secondary"), &right)) { step.buttonMask ^= 2; changed = true; }
        ImGui::SameLine(0.0f, 8.0f);

        ImGui::PushItemWidth(150.0f);
        changed |= UISlider("##stepDur", &step.durationMs, 0.0f, 1000.0f, Number(0, " ms"));
        ImGui::PopItemWidth();
        ImGui::SameLine(0.0f, 8.0f);

        ImGui::TextUnformatted(L("+/-"));
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::PushItemWidth(96.0f);
        changed |= UISlider("##stepJitter", &step.jitterMs, 0.0f, 50.0f, Number(0, " ms"));
        ImGui::PopItemWidth();
        ImGui::SameLine(0.0f, 8.0f);

        if (ImGui::Button(L("Remove"))) {
            settings.sequenceSteps.erase(settings.sequenceSteps.begin() + index);
            changed = true;
            ImGui::PopID();
            break;
        }

        ImGui::PopID();
    }

    if (static_cast<int>(settings.sequenceSteps.size()) < OW::Config::kMaxHeroSkillSequenceSteps) {
        if (ImGui::Button(L("Add Step"))) {
            settings.sequenceSteps.push_back({ 1, 117, 1.0f, 0 });
            changed = true;
        }
    }

    if (!definition.defaultSettings.sequenceSteps.empty()) {
        ImGui::SameLine(0.0f, 8.0f);
        if (ImGui::Button(L("Reset Steps"))) {
            settings.sequenceSteps = definition.defaultSettings.sequenceSteps;
            changed = true;
        }
    }

    return changed;
}

static bool DrawHeroSkillDefinition(const OW::HeroSkillDefinition& definition, uint64_t heroId) {
    OW::Config::HeroSkillSettings settings = OW::Config::GetHeroSkillSettings(
        heroId,
        definition.skillId ? definition.skillId : "",
        definition.defaultSettings);

    bool changed = false;
    auto hasControl = [&](OW::HeroSkillControlFlags control) {
        return OW::HasHeroSkillControl(definition, control);
    };
    ImGui::PushID(definition.skillId);

    if (ID3D11ShaderResourceView* icon = HeroSkillIconForDefinition(definition)) {
        ImGui::Image(reinterpret_cast<ImTextureID>(icon), ImVec2(24.0f, 24.0f));
        ImGui::SameLine(0.0f, 8.0f);
    }
    if (!hasControl(OW::HeroSkillControls::PausePolicy)) {
      ImGui::AlignTextToFramePadding();
      ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kColTextMuted), "%s / %s",
                       L(OW::HeroSkillCategoryName(definition.category)),
                       L(OW::HeroSkillInputActionName(definition.inputAction)));
    }

    if (hasControl(OW::HeroSkillControls::Enabled)) {
        SettingRow("Enabled", kAimbotRightLabelWidth);
        changed |= UICheckbox("##skillEnabled", &settings.enabled);
    }

    const bool hasSequenceControls = hasControl(OW::HeroSkillControls::SequenceSteps);
    const bool hasTrackingOverlay = hasControl(OW::HeroSkillControls::TrackingOverlay);
    const bool hasComboAction = hasControl(OW::HeroSkillControls::ComboAction);
    const bool hasPitchControls = hasControl(OW::HeroSkillControls::PitchControl);
    const bool hasPhaseTiming = hasControl(OW::HeroSkillControls::PhaseTiming);
    const bool hasHealthThreshold = hasControl(OW::HeroSkillControls::HealthThreshold);
    const bool hasAbsoluteHealth = hasControl(OW::HeroSkillControls::HealthAbsolute);
    const bool hasAbsoluteEnemyHealth = hasControl(OW::HeroSkillControls::EnemyHealthAbsolute);
    const bool hasPunchAssist = hasControl(OW::HeroSkillControls::PunchAssist);
    if (hasControl(OW::HeroSkillControls::PausePolicy)) {
        SettingRow("Pause Aim", kAimbotRightLabelWidth);
        changed |= UICheckbox("##pauseAim", &settings.pauseAim);
        SettingRow("Pause Trigger", kAimbotRightLabelWidth);
        changed |= UICheckbox("##pauseTrigger", &settings.pauseTrigger);
        SettingRow("Pause Other Skills", kAimbotRightLabelWidth);
        changed |= UICheckbox("##pauseSkills", &settings.pauseSkills);
        ImGui::TextWrapped("%s", L("Pauses assists during the active defensive skill. Rocket Punch pauses while the secondary-fire key is held. Your enabled settings are restored when the state ends."));
        if (changed) OW::Config::SetHeroSkillSettings(heroId, definition.skillId, settings);
        ImGui::PopID();
        return changed;
    }
    if (hasControl(OW::HeroSkillControls::DefensiveConditions)) {
        SettingRow("Low Health Condition", kAimbotRightLabelWidth);
        changed |= UICheckbox("##healthTrigger", &settings.healthTriggerEnabled);
        SettingRow("Incoming Threat Condition", kAimbotRightLabelWidth);
        changed |= UICheckbox("##threatTrigger", &settings.threatTriggerEnabled);
        ImGui::TextUnformatted(L("Normal Skill Threats"));
        for (size_t i = 0; i < OW::SkillConditions::kNormalThreats.size(); ++i) {
            if(!OW::SkillConditions::kNormalThreats[i].incoming) continue;
            bool selected = (settings.normalThreatMask & (1 << i)) != 0;
            if (UICheckbox(L(OW::SkillConditions::kNormalThreats[i].label), &selected)) {
                settings.normalThreatMask ^= 1 << i;
                changed = true;
            }
        }
        ImGui::TextWrapped("%s", L("Ultimate threats are waiting for verified activation signals."));
    }
    if(definition.automation==OW::SkillAutomation::NormalInterrupt) {
        ImGui::TextWrapped("%s",L("Interrupts use fresh skill activation samples; hit timing still needs in-game verification. Ultimate interrupts await activation signals."));
        ImGui::TextUnformatted(L("Normal Skill Threats"));
        for(size_t i=0;i<OW::SkillConditions::kNormalThreats.size();++i) {
            if(OW::SkillConditions::kNormalThreats[i].interruptWindowMs<=0) continue;
            bool selected=(settings.normalThreatMask & (1<<i))!=0;
            if(UICheckbox(L(OW::SkillConditions::kNormalThreats[i].label),&selected)) {
                settings.normalThreatMask^=1<<i;changed=true;
            }
        }
    }
    if(definition.automation==OW::SkillAutomation::NormalInterrupt ||
       definition.automation==OW::SkillAutomation::LowEnemyHealth) {
        SettingRow("Aim Before Casting",kAimbotRightLabelWidth);
        changed|=UICheckbox("##autoSkillAim",&settings.automaticAim);
    }
    if (definition.automation == OW::SkillAutomation::GenjiBlade) {
        ImGui::TextWrapped("%s", L("Manually use the mapped ultimate key at full charge. After drawing the blade, assists aim, dashes beyond 5 m and swings within 5 m. Ends after 7 seconds. Does not cast Q. Timing needs in-game calibration."));
        if (ImGui::SmallButton(L("Edit hero action keys..."))) UI::state.activeTab=UI::TAB_KEYS;
    }
    if (definition.automation == OW::SkillAutomation::RecentDamageDeflect)
        ImGui::TextWrapped("%s", L("Deflects at low health after recent damage reaches 5% of maximum health, with a visible enemy in range. The damage window expires 450 ms after the last hit. Does not identify incoming projectiles or turn the camera."));
    if (definition.automation == OW::SkillAutomation::HealthSurvival) {
        ImGui::TextWrapped("%s", L("Uses the mapped action at low self health. Requires fresh health and ability samples. The interval limits attempts; it does not prove a charge or resource is available."));
        if (definition.heroId == OW::eHero::HERO_ROADHOG)
            ImGui::TextWrapped("%s", L("Enable Toggle Take a Breather in the game before using this helper. It taps Ability 2; it does not hold the key through healing."));
        if (definition.inputAction == OW::HeroSkillInputAction::Ultimate)
            ImGui::TextWrapped("%s", L("Requires fresh full ultimate charge and low self health. Enemy ultimate detection is a separate condition."));
    }
    if (definition.automation == OW::SkillAutomation::ShieldBash)
        ImGui::TextWrapped("%s", L("Hold the mapped secondary-fire key to raise the shield. Taps primary fire only while a visible enemy is under the crosshair and in range. Does not turn the camera or detect ultimate stuns; the interval limits attempts."));
    if (definition.heroId == OW::eHero::HERO_GENJI && definition.inputAction == OW::HeroSkillInputAction::Ability1)
        ImGui::TextWrapped("%s", L("Dash uses fresh activation samples and a bounded retry interval; kill resets and actual recharge are not yet directly readable."));
    if(definition.automation==OW::SkillAutomation::RecastReturn)
        ImGui::TextWrapped("%s",L("Recasts the mapped Ability 1 only after a freshly observed deployment. Does not deploy a return point at low health."));
    if(definition.automation==OW::SkillAutomation::EmreJump) {
        SettingRow("Forward Duration",kAimbotRightLabelWidth);PushControlWidth();
        changed|=UISlider("##emreMoveMs",&settings.moveForwardMs,50.f,1000.f,Number(0," ms"));ImGui::PopItemWidth();
        SettingRow("Grenade To Movement Delay",kAimbotRightLabelWidth);PushControlWidth();
        changed|=UISlider("##emreLeadMs",&settings.jumpLeadMs,0.f,500.f,Number(0," ms"));ImGui::PopItemWidth();
        ImGui::TextWrapped("%s",L("Press once: throw E at the ground, use Shift and move forward, then jump. Timing is adjustable and needs in-game calibration."));
    }
    if (std::string(definition.skillId) == "transcendence")
        ImGui::TextWrapped("%s", L("Automatic Transcendence is waiting for a verified enemy ultimate activation signal. Enemy charge alone does not trigger it."));
    if (hasControl(OW::HeroSkillControls::GroupCondition) && settings.radius <= 0)
        ImGui::TextWrapped("%s", L("Set the verified effect radius before enabling automatic group healing. A zero radius does not cast."));
    const bool hasSkillOutputKey = !hasSequenceControls && !hasComboAction && !hasPunchAssist &&
        (hasControl(OW::HeroSkillControls::Key) || hasPitchControls ||
         hasPhaseTiming || hasHealthThreshold || hasAbsoluteHealth);

    if (hasControl(OW::HeroSkillControls::Key)) {
        SettingRow("Activation Key", kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISelect("##skillKey", &settings.key,
                            OW::Labels::kAimActivationKeys,
                            OW::Labels::AimActivationKeyCount());
        ImGui::PopItemWidth();
    }

    if (hasSkillOutputKey && ImGui::SmallButton(L("Edit hero action keys...")))UI::state.activeTab=UI::TAB_KEYS;


    if ((hasSequenceControls || hasPitchControls || hasPhaseTiming) &&
        !hasControl(OW::HeroSkillControls::Key)) {
        SettingRow("Activation Key", kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISelect("##skillActivationKey", &settings.key,
                            OW::Labels::kAimActivationKeys,
                            OW::Labels::AimActivationKeyCount());
        ImGui::PopItemWidth();
    }

    if (hasControl(OW::HeroSkillControls::Mode)) {
        SettingRow("Mode", kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISelect("##skillMode", &settings.mode, kHeroSkillModes, IM_ARRAYSIZE(kHeroSkillModes));
        ImGui::PopItemWidth();
    }

    if (hasHealthThreshold) {
        SettingRow("Health Threshold", kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISlider("##skillHealth", &settings.healthThreshold, 0.0f, 100.0f, Number(0, "%"));
        ImGui::PopItemWidth();
    }

    if (hasAbsoluteHealth) {
        SettingRow("Current HP Threshold", kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISlider("##skillSelfMaxHealth", &settings.healthThreshold, 0.0f, 500.0f, Number(0, " HP"));
        ImGui::PopItemWidth();
    }

    if (hasControl(OW::HeroSkillControls::MeleeRange) && settings.enabled) {
        const char* modes[] = { "Any target health", "Current health percentage", "Current health amount" };
        SettingRow("Melee Trigger", kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISelect("##meleeHealthMode", &settings.meleeHealthMode, modes, IM_ARRAYSIZE(modes));
        ImGui::PopItemWidth();
        if (settings.meleeHealthMode == OW::Config::kMeleeHealthPercent) {
            SettingRow("Current HP Percentage Threshold", kAimbotRightLabelWidth);
            PushControlWidth();
            changed |= UISlider("##meleeHealthPercent", &settings.meleeHealthPercent, 0.0f, 100.0f, Number(0, "%"));
            ImGui::PopItemWidth();
            ImGui::TextWrapped("%s", L("Uses current total HP divided by maximum total HP. Triggers at or below the selected percentage."));
        }
    }

    if (hasAbsoluteEnemyHealth && (!hasControl(OW::HeroSkillControls::MeleeRange) ||
        (settings.enabled && settings.meleeHealthMode == OW::Config::kMeleeHealthAbsolute))) {
        SettingRow(hasControl(OW::HeroSkillControls::MeleeRange) ? "Current HP Threshold" : "Max Health", kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISlider("##skillEnemyMaxHealth", &settings.enemyHealthThreshold, 0.0f,
                            hasControl(OW::HeroSkillControls::MeleeRange) ? 500.0f : 300.0f, Number(0, " HP"));
        ImGui::PopItemWidth();
    }

    if (hasControl(OW::HeroSkillControls::EnemyHealthThreshold)) {
        SettingRow("Enemy HP Threshold", kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISlider("##skillEnemyHealth", &settings.enemyHealthThreshold, 0.0f, 100.0f, Number(0, "%"));
        ImGui::PopItemWidth();
    }

    if (hasControl(OW::HeroSkillControls::AllyHealthThreshold)) {
        SettingRow("Ally HP Threshold", kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISlider("##skillAllyHealth", &settings.allyHealthThreshold, 0.0f, 100.0f, Number(0, "%"));
        ImGui::PopItemWidth();
    }

    if (hasControl(OW::HeroSkillControls::Distance)) {
        SettingRow(hasAbsoluteEnemyHealth ? "Max Distance" : "Distance", kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISlider("##skillDistance", &settings.distance, 0.0f, hasPunchAssist ? 20.0f : 100.0f,
                            Number(1, " m"));
        ImGui::PopItemWidth();
    }

    if (hasControl(OW::HeroSkillControls::Radius)) {
        SettingRow("Radius", kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISlider("##skillRadius", &settings.radius, 0.0f, 30.0f, Number(1, " m"));
        ImGui::PopItemWidth();
    }

    if (hasControl(OW::HeroSkillControls::Prediction)) {
        SettingRow("Prediction", kAimbotRightLabelWidth);
        changed |= UICheckbox("##skillPrediction", &settings.prediction);
    }

    if (hasControl(OW::HeroSkillControls::CooldownGuard)) {
        SettingRow("Cooldown Guard", kAimbotRightLabelWidth);
        changed |= UICheckbox("##skillCooldownGuard", &settings.cooldownGuard);
    }

    if (hasControl(OW::HeroSkillControls::Cooldown)) {
        SettingRow("Cooldown", kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISlider("##skillCooldown", &settings.cooldown, 0.0f, 60.0f, Number(1, " s"));
        ImGui::PopItemWidth();
    }

    if (hasControl(OW::HeroSkillControls::AmmoGuard)) {
        SettingRow("Ammo Guard", kAimbotRightLabelWidth);
        changed |= UICheckbox("##skillAmmoGuard", &settings.ammoGuard);

        if (settings.ammoGuard) {
            SettingRow("Reserve Ammo", kAimbotRightLabelWidth);
            PushControlWidth();
            changed |= UISlider("##skillAmmoReserve", &settings.ammoGuardReserve, 0.0f, 12.0f, Number(0, ""));
            ImGui::PopItemWidth();
        }
    }

    if (hasControl(OW::HeroSkillControls::MinTargets)) {
        SettingRow("Min Targets", kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISlider("##skillMinTargets", &settings.minTargets, 1.0f, 8.0f, Number(0, ""));
        ImGui::PopItemWidth();
    }

    if (hasControl(OW::HeroSkillControls::Bone)) {
        SettingRow("Bone", kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISelect("##skillTargetBone", &settings.tracking.bone,
                            kHeroSkillTargetBones, IM_ARRAYSIZE(kHeroSkillTargetBones));
        ImGui::PopItemWidth();
    }

    if (hasControl(OW::HeroSkillControls::MeleeRange)) {
        SettingRow("Melee Range Expansion", kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISlider("##skillMeleeRadius", &settings.meleeRadius, 0.0f, 2.0f, Number(2, " m"));
        ImGui::PopItemWidth();
        ImGui::TextWrapped("%s", L("Checks the whole body within Max Distance. Current total HP includes remaining health, armor, shields and extra health. Directly writes the game's melee action for 50 ms; no Box keyboard or activation key is needed."));
    }

    if (hasControl(OW::HeroSkillControls::Hitbox)) {
        SettingRow("Hitbox Scale", kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISlider("##skillTargetHitbox", &settings.tracking.hitbox,
                            OW::Config::kMinHitboxScalePercent,
                            OW::Config::kMaxHitboxScalePercent, Number(0, " %"));
        ImGui::PopItemWidth();
    }

    if (hasTrackingOverlay && settings.enabled) {
        const bool canMoveAim=definition.automation==OW::SkillAutomation::None ||
            definition.automation==OW::SkillAutomation::GenjiBlade ||
            (settings.automaticAim && (definition.automation==OW::SkillAutomation::LowEnemyHealth ||
                definition.automation==OW::SkillAutomation::NormalInterrupt));
        ImGui::Spacing();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kColTextMuted), "%s", L("Skill Aim"));

        if(canMoveAim) {
        SettingRow("Aim Behavior", kAimbotRightLabelWidth);
        PushControlWidth();
        if (UISelect("##skillTrackingBehavior", &settings.tracking.aimBehavior,
                     kAimBehavior, IM_ARRAYSIZE(kAimBehavior))) {
            settings.tracking.aimBehavior =
                OW::Config::ClampAimBehaviorIndex(settings.tracking.aimBehavior);
            settings.tracking.aimBehaviorPresetId = -1;
            changed = true;
        }
        ImGui::PopItemWidth();

        SettingRow("Behavior Preset", kAimbotRightLabelWidth);
        PushControlWidth();
        if (UISelectAimBehaviorPreset("##skillTrackingBehaviorPreset", &settings.tracking.aimBehaviorPresetId, -1)) {
            if (const OW::Config::AimBehaviorPreset* behaviorPreset =
                    OW::Config::FindAimBehaviorPreset(settings.tracking.aimBehaviorPresetId)) {
                settings.tracking.aimBehavior =
                    OW::Config::ClampAimBehaviorIndex(behaviorPreset->behavior);
            }
            changed = true;
        }
        ImGui::PopItemWidth();
        }
        {
            SettingRow("Priority", kAimbotRightLabelWidth);
            PushControlWidth();
            const char* priorities[] = {L("FOV"), L("Health"), L("Distance")};
            changed |= UISelect("##skillPriority", &settings.targetPriority, priorities, IM_ARRAYSIZE(priorities));
            ImGui::PopItemWidth();

            SettingRow("Assistance Delay", kAimbotRightLabelWidth);
            PushControlWidth();
            changed |= UISlider("##skillAssistanceDelay", &settings.assistanceDelayMs, 0.0f, 1000.0f, Number(0, " ms"));
            ImGui::PopItemWidth();
        }
        SettingRow("Require Visible", kAimbotRightLabelWidth);
        changed |= UICheckbox("##skillRequireVisible", &settings.requireVisible);
        SettingRow("Allow Invisible Sombra", kAimbotRightLabelWidth);
        changed |= UICheckbox("##skillStealth", &settings.allowInvisibleSombra);
        SettingRow("Target Roles", kAimbotRightLabelWidth);
        const char* roleNames[] = {L("Damage"), L("Tank"), L("Support")};
        for (int i = 0; i < 3; ++i) {
            bool selected = (settings.targetRoleMask & (1 << i)) != 0;
            ImGui::PushID(i);
            if (UICheckbox(roleNames[i], &selected)) { settings.targetRoleMask ^= 1 << i; changed = true; }
            ImGui::PopID();
            if (i != 2) ImGui::SameLine();
        }
        SettingRow("Draw FOV", kAimbotRightLabelWidth);
        changed |= UICheckbox("##skillDrawFov", &settings.drawSkillFov);
        ImGui::SameLine();
        changed |= UI::ColorPicker("skillFovColor", settings.skillFovColor);

        if(canMoveAim) {
        SettingRow("Speed Scale", kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISlider("##skillTrackingSpeedScale", &settings.tracking.speedScale, 0.0f, 100.0f, Number(0, " %"));
        ImGui::PopItemWidth();
        if(hasControl(OW::HeroSkillControls::TrajectoryAction) || definition.automation!=OW::SkillAutomation::None) {
            SettingRow("Horizontal Motion Scale",kAimbotRightLabelWidth);PushControlWidth();
            changed|=UISlider("##skillYawScale",&settings.tracking.yawScale,0.f,200.f,Number(0," %"));ImGui::PopItemWidth();
            SettingRow("Vertical Motion Scale",kAimbotRightLabelWidth);PushControlWidth();
            changed|=UISlider("##skillPitchScale",&settings.tracking.pitchScale,0.f,200.f,Number(0," %"));ImGui::PopItemWidth();
        }
        }

        SettingRow("FOV (deg)", kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISlider("##skillTrackingFov", &settings.tracking.fov,
                            OW::Config::kMinFovDeg, OW::Config::kMaxFovDeg, Number(0, " deg"));
        ImGui::PopItemWidth();

        SettingRow("Bone", kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISelect("##skillTrackingBone", &settings.tracking.bone,
                            kHeroSkillTrackingBones, IM_ARRAYSIZE(kHeroSkillTrackingBones));
        ImGui::PopItemWidth();

        SettingRow("Hitbox Scale", kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISlider("##skillTrackingHitbox", &settings.tracking.hitbox,
                            OW::Config::kMinHitboxScalePercent,
                            OW::Config::kMaxHitboxScalePercent, Number(0, " %"));
        ImGui::PopItemWidth();

        if (hasSequenceControls) {
            ImGui::Spacing();
            ImGui::TextColored(
                ImGui::ColorConvertU32ToFloat4(kColTextMuted),
                "%s", L("Shot Phase Tracking"));

            SettingRow("Phase Aware", kAimbotRightLabelWidth);
            changed |= UICheckbox(
                "##sequencePhaseAwareTracking",
                &settings.sequencePhaseAwareTracking);

            if (settings.sequencePhaseAwareTracking) {
                SettingRow("Post-fire Pause", kAimbotRightLabelWidth);
                PushControlWidth();
                changed |= UISlider(
                    "##sequencePostFirePause",
                    &settings.sequencePostFirePauseMs,
                    0.0f,
                    500.0f, Number(0, " ms"));
                ImGui::PopItemWidth();

                SettingRow("Recovery", kAimbotRightLabelWidth);
                PushControlWidth();
                changed |= UISlider(
                    "##sequenceRecovery",
                    &settings.sequenceRecoveryMs,
                    0.0f,
                    1000.0f, Number(0, " ms"));
                ImGui::PopItemWidth();

                SettingRow("Pause X Scale", kAimbotRightLabelWidth);
                PushControlWidth();
                changed |= UISlider(
                    "##sequencePostFireYawScale",
                    &settings.sequencePostFireYawScale,
                    0.0f,
                    1.0f, Number(2, "x"));
                ImGui::PopItemWidth();

                SettingRow("Pause Y Scale", kAimbotRightLabelWidth);
                PushControlWidth();
                changed |= UISlider(
                    "##sequencePostFirePitchScale",
                    &settings.sequencePostFirePitchScale,
                    0.0f,
                    1.0f, Number(2, "x"));
                ImGui::PopItemWidth();

                SettingRow("Pre-shot Window", kAimbotRightLabelWidth);
                PushControlWidth();
                changed |= UISlider(
                    "##sequencePreFireBoostWindow",
                    &settings.sequencePreFireBoostWindowMs,
                    0.0f,
                    500.0f, Number(0, " ms"));
                ImGui::PopItemWidth();

                SettingRow("Pre-shot Boost", kAimbotRightLabelWidth);
                PushControlWidth();
                changed |= UISlider(
                    "##sequencePreFireBoostScale",
                    &settings.sequencePreFireBoostScale,
                    1.0f,
                    3.0f, Number(2, "x"));
                ImGui::PopItemWidth();
            }
        }

        if (hasControl(OW::HeroSkillControls::Prediction)) {
            SettingRow("Projectile Speed", kAimbotRightLabelWidth);
            PushControlWidth();
            changed |= UISlider("##skillProjectileSpeed", &settings.projectileSpeed, 0.0f, 300.0f, Number(0, " m/s"));
            ImGui::PopItemWidth();

            SettingRow("Projectile Radius", kAimbotRightLabelWidth);
            PushControlWidth();
            changed |= UISlider("##skillProjectileRadius", &settings.projectileRadius, 0.0f, 2.0f, Number(1, " m"));
            ImGui::PopItemWidth();

            SettingRow("Projectile Gravity", kAimbotRightLabelWidth);
            changed |= UICheckbox("##skillProjectileGravity", &settings.projectileGravity);

            SettingRow("Aim Wind-up", kAimbotRightLabelWidth);
            PushControlWidth();
            changed |= UISlider("##skillPreFireDelay", &settings.preFireDelayMs, 0.0f, 1000.0f, Number(0, " ms"));
            ImGui::PopItemWidth();
        }
        if (hasControl(OW::HeroSkillControls::Prediction) || hasControl(OW::HeroSkillControls::TrajectoryAction)) {
            SettingRow("Max Aim Time", kAimbotRightLabelWidth);
            PushControlWidth();
            changed |= UISlider("##skillMaxAimTime", &settings.maxAimTimeMs, 0.0f, 5000.0f, Number(0, " ms"));
            ImGui::PopItemWidth();
        }
    }

    auto drawFloatSlider = [&](const char* rowLabel,
                               const char* id,
                               float& value,
                               float minValue,
                               float maxValue,
                               const Spec& format) {
        SettingRow(rowLabel, kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISlider(id, &value, minValue, maxValue, format);
        ImGui::PopItemWidth();
    };

    auto drawIntSlider = [&](const char* rowLabel,
                             const char* id,
                             int& value,
                             float minValue,
                             float maxValue,
                             const Spec& format) {
        SettingRow(rowLabel, kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISlider(id, &value, minValue, maxValue, format);
        ImGui::PopItemWidth();
    };

    if (hasPunchAssist) {
        ImGui::TextWrapped("%s", L("Hold RMB to charge; press the activation key to aim horizontally and release. Release RMB before the next attempt."));
        ImGui::TextWrapped("%s", L("Charge timing is estimated from input. Height limits do not check stairs or obstacles."));
        drawFloatSlider("Max Height Above", "##punchAbove", settings.punchMaxHeightAbove, 0.0f, 5.0f, Number(1, " m"));
        drawFloatSlider("Max Height Below", "##punchBelow", settings.punchMaxHeightBelow, 0.0f, 5.0f, Number(1, " m"));
        drawFloatSlider("Full Charge Time", "##punchCharge", settings.punchFullChargeMs, 200.0f, 2000.0f, Number(0, " ms"));
        drawFloatSlider("Auto-release Total Time", "##punchMaxHold", settings.punchMaxHoldMs, settings.punchFullChargeMs + 100.0f, 5000.0f, Number(0, " ms"));
        drawFloatSlider("Release Lead", "##punchLead", settings.punchReleaseLeadMs, 20.0f, settings.punchMaxHoldMs - 50.0f, Number(0, " ms"));
        ImGui::TextWrapped("%s", L("Full charge sets punch strength. Total time includes charging and holding before automatic release; both times are estimates."));
        drawFloatSlider("FOV (deg)", "##punchFov", settings.tracking.fov, OW::Config::kMinFovDeg, OW::Config::kMaxFovDeg, Number(0, " deg"));
        drawFloatSlider("Speed Scale", "##punchSpeed", settings.tracking.speedScale, 0.0f, 100.0f, Number(0, " %"));
        drawFloatSlider("Hitbox Scale", "##punchHitbox", settings.tracking.hitbox, OW::Config::kMinHitboxScalePercent, OW::Config::kMaxHitboxScalePercent, Number(0, " %"));
        if (!V2::Output::SupportsMouseMask(V2::Output::ActiveRuntimeSnapshot().descriptor))
            ImGui::TextWrapped("%s", L("Rocket Punch assist needs mouse masking from the output device."));
    }

    if (hasPitchControls) {
        ImGui::Spacing();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kColTextMuted), "%s", L("Pitch Down"));

        drawIntSlider("Duration", "##pitchDownDuration", settings.pitchDownDurationMs, 20.0f, 100.0f, Number(0, " ms"));
        drawFloatSlider("Duration Jitter", "##pitchDownDurationJitter", settings.pitchDownDurationJitter, 0.0f, 50.0f, Number(0, " ms", 1, "+/- "));
        drawFloatSlider("Target Angle", "##pitchDownTarget", settings.pitchDownTargetAngle, 0.0f, 180.0f, Number(0, " deg"));
    }

    if (hasPitchControls || hasPhaseTiming) {
        ImGui::Spacing();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kColTextMuted), "%s", L("Pitch Up"));

        if (hasPitchControls) {
            drawFloatSlider("Return Jitter", "##pitchUpOffsetJitter", settings.pitchUpOffsetJitter, 0.0f, 20.0f, Number(1, " deg", 1, "+/- "));
        }

        if (hasPhaseTiming) {
            drawIntSlider("Fire Delay", "##skillFireDelay", settings.fireDelayMs, 0.0f, 100.0f, Number(0, " ms"));

        }
    }

    if (changed)
        OW::Config::SetHeroSkillSettings(heroId, definition.skillId ? definition.skillId : "", settings);

    ImGui::PopID();
    return changed;
}

static bool DrawHeroSkillSequenceDefinition(const OW::HeroSkillDefinition& definition, uint64_t heroId) {
    OW::Config::HeroSkillSettings settings = OW::Config::GetHeroSkillSettings(
        heroId,
        definition.skillId ? definition.skillId : "",
        definition.defaultSettings);

    ImGui::PushID(definition.skillId);
    if (ID3D11ShaderResourceView* icon = HeroSkillIconForDefinition(definition)) {
        ImGui::Image(reinterpret_cast<ImTextureID>(icon), ImVec2(24.0f, 24.0f));
        ImGui::SameLine(0.0f, 8.0f);
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kColTextMuted), "%s / %s",
                       L(OW::HeroSkillCategoryName(definition.category)),
                       L(OW::HeroSkillInputActionName(definition.inputAction)));

    const bool changed = DrawHeroSkillSequenceSteps(definition, settings);
    if (changed)
        OW::Config::SetHeroSkillSettings(heroId, definition.skillId ? definition.skillId : "", settings);

    ImGui::PopID();
    return changed;
}

// =====================================================================
// UI::SkillsPage
// =====================================================================
void UI::SkillsPage() {
    ImGui::PushID("SkillsPage");

    const HeroOption& selectedHero = CurrentHeroOption();
    auto heroSettings=V2::HeroSettings::Get(selectedHero.heroId);
    auto& automatic=heroSettings.automatic;
    const auto previous=automatic;
    ImGui::BeginDisabled(selectedHero.heroId==0);
    UIGroupBox("Auto Alt-Fire");
    {
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", L("Tap RMB when the selected enemy is below the health and distance limits."));
        SettingRow("Enabled", kDefaultLabelWidth);
        UICheckbox("##autoRmbEnable", &automatic.altFire);

        SettingRow("Health Threshold", kDefaultLabelWidth);
        PushControlWidth();
        UISlider("##autoRmbHealth", &automatic.altFireHealth, 0.0f, 500.0f, Number(0, " HP"));
        ImGui::PopItemWidth();

        SettingRow("Distance", kDefaultLabelWidth);
        PushControlWidth();
        UISlider("##autoRmbDistance", &automatic.altFireDistance, 0.0f, 60.0f, Number(1, " m"));
        ImGui::PopItemWidth();
    }
    CloseGroupBox();

    UIGroupBox("Auto Skill");
    {
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", L("Use a supported hero's defensive ability at low self health."));
        SettingRow("Enabled", kDefaultLabelWidth);
        UICheckbox("##autoSkillEnable", &automatic.defensive);

        SettingRow("Health Threshold", kDefaultLabelWidth);
        PushControlWidth();
        UISlider("##skillHealth", &automatic.defensiveHealth, 0.0f, 500.0f, Number(0, " HP"));
        ImGui::PopItemWidth();
    }
    CloseGroupBox();

    UIGroupBox("Auto Shoot");
    {
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", L("Legacy repeat-fire timer; this does not enable an individual hero skill."));
        SettingRow("Enabled", kDefaultLabelWidth);
        UICheckbox("##autoShootEnable", &automatic.repeatFire);

        SettingRow("Cooldown", kDefaultLabelWidth);
        PushControlWidth();
        UISlider("##shootCooldown", &automatic.repeatIntervalMs, 100, 2000, Number(0, " ms"));
        ImGui::PopItemWidth();
    }
    CloseGroupBox();

    ImGui::EndDisabled();
    if(automatic.altFire!=previous.altFire||automatic.defensive!=previous.defensive||automatic.repeatFire!=previous.repeatFire||
        automatic.altFireHealth!=previous.altFireHealth||automatic.altFireDistance!=previous.altFireDistance||
        automatic.defensiveHealth!=previous.defensiveHealth||automatic.repeatIntervalMs!=previous.repeatIntervalMs)
        V2::HeroSettings::Set(selectedHero.heroId,heroSettings);
    int renderedSkillCount = 0;
    if (selectedHero.heroId != 0) {
        for (const OW::HeroSkillDefinition& definition : OW::AllHeroSkillDefinitions()) {
            if (!OW::IsHeroSkillDefinitionActiveForRuntime(definition, selectedHero.heroId))
                continue;

            std::string title = L(selectedHero.label);
            title += " / ";
            title += L(definition.displayName);
            UIGroupBox(title.c_str());
            DrawHeroSkillDefinition(definition, selectedHero.heroId);
            CloseGroupBox();
            ++renderedSkillCount;
        }
    }

    if (renderedSkillCount == 0) {
        UIGroupBox("Hero Skills");
        {
            ImGui::TextDisabled("%s", L("No configurable skills"));
        }
        CloseGroupBox();
    }

    ImGui::PopID();
}

// =====================================================================
// UI::Combo page
// =====================================================================
void UI::SequencesPage() {
    ImGui::PushID("SequencesPage");

    const HeroOption& selectedHero = CurrentHeroOption();
    int renderedSequenceCount = 0;

    if (selectedHero.heroId != 0) {
        for (const OW::HeroSkillDefinition& definition : OW::AllHeroSkillDefinitions()) {
            if (!OW::IsHeroSkillDefinitionActiveForRuntime(definition, selectedHero.heroId))
                continue;

            const bool sequenceCombo =
                OW::HasHeroSkillControl(definition, OW::HeroSkillControls::SequenceSteps);
            const bool dedicatedCombo =
                OW::HasHeroSkillControl(definition, OW::HeroSkillControls::ComboAction);
            if (!sequenceCombo && !dedicatedCombo) {
                continue;
            }

            std::string title = L(selectedHero.label);
            title += " / ";
            title += L(definition.displayName);
            UIGroupBox(title.c_str());
            if (sequenceCombo)
                DrawHeroSkillSequenceDefinition(definition, selectedHero.heroId);
            else
                DrawHeroSkillDefinition(definition, selectedHero.heroId);
            CloseGroupBox();
            ++renderedSequenceCount;
        }
    }

    if (renderedSequenceCount == 0) {
        UIGroupBox("Combo");
        {
            ImGui::TextDisabled("%s", L("No configurable combos"));
        }
        CloseGroupBox();
    }

    ImGui::PopID();
}

// =====================================================================
// UI::VisualsPage
// =====================================================================
