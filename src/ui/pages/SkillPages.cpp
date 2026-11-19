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
using namespace UI::Theme;

static bool DrawHeroSkillSequenceSteps(const OW::HeroSkillDefinition& definition,
                                       OW::Config::HeroSkillSettings& settings) {
    bool changed = false;

    ImGui::Spacing();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kColTextMuted), "%s", L("Sequence Steps"));

    if (!settings.sequenceSteps.empty()) {
        float speedScale = settings.sequenceSteps.front().speedScale;
        SettingRow("Speed Scale");
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

static void SkillTooltip(const OW::HeroSkillDefinition& definition) {
    if (!BeginSettingTooltip())
        return;
    using Automation = OW::SkillAutomation;
    switch (definition.automation) {
    case Automation::NormalInterrupt:
        ImGui::TextUnformatted(L("Interrupts use fresh skill activation samples; hit timing still needs in-game verification. Ultimate interrupts await activation signals."));
        break;
    case Automation::GenjiBlade:
        ImGui::TextUnformatted(L("Manually use the mapped ultimate key at full charge. After drawing the blade, assists aim, dashes beyond 5 m and swings within 5 m. Ends after 7 seconds. Does not cast Q. Timing needs in-game calibration."));
        break;
    case Automation::MercyBeam:
        ImGui::TextUnformatted(L("Hold the activation key: heal the connected ally at or below the threshold; damage boost above it. Releasing the key returns manual control. Requires mouse primary/secondary mappings and physical-button masking. Connection reading is a candidate awaiting in-game verification."));
        break;
    case Automation::LucioSong:
        ImGui::TextUnformatted(L("Switches to healing when enough nearby allies meet the health threshold. Returns to speed after the condition stays clear for 0.8 seconds. Waits for an observed song change before toggling again. Song reading is a candidate awaiting in-game verification."));
        ImGui::TextUnformatted(L("Use toggle Crossfade in the game; hold-to-crossfade is not supported."));
        break;
    case Automation::ProjectileDefense:
        ImGui::TextUnformatted(L("Uses enemy team and consecutive projectile positions to estimate an approach toward you within 250 ms. Unknown, stationary, friendly and already handled objects are ignored. Only the listed projectiles are supported; resources and gameplay interception still need verification."));
        break;
    case Automation::RecentDamageDeflect:
        ImGui::TextUnformatted(L("Deflects at low health after recent damage reaches 5% of maximum health, with a visible enemy in range. The damage window expires 450 ms after the last hit. Does not identify incoming projectiles or turn the camera."));
        break;
    case Automation::HealthSurvival:
        ImGui::TextUnformatted(L("Uses the mapped action at low self health. Requires fresh health and ability samples. The interval limits attempts; it does not prove a charge or resource is available."));
        if (definition.heroId == OW::eHero::HERO_ROADHOG)
            ImGui::TextUnformatted(L("Enable Toggle Take a Breather in the game before using this helper. It taps Ability 2; it does not hold the key through healing."));
        if (definition.inputAction == OW::HeroSkillInputAction::Ultimate)
            ImGui::TextUnformatted(L("Requires fresh full ultimate charge and low self health. Enemy ultimate detection is a separate condition."));
        break;
    case Automation::ShieldBash:
        ImGui::TextUnformatted(L("Hold the mapped secondary-fire key to raise the shield. Taps primary fire only while a visible enemy is under the crosshair and in range. Does not turn the camera or detect ultimate stuns; the interval limits attempts."));
        break;
    case Automation::RecastReturn:
        ImGui::TextUnformatted(L("Recasts the mapped Ability 1 only after a freshly observed deployment. Does not deploy a return point at low health."));
        break;
    case Automation::EmreJump:
        ImGui::TextUnformatted(L("Press once: throw E at the ground, use Shift and move forward, then jump. Timing is adjustable and needs in-game calibration."));
        break;
    default:
        break;
    }

    const char* id = definition.skillId ? definition.skillId : "";
    if (definition.heroId == OW::eHero::HERO_ASHE && std::strcmp(id, "fire-pattern") == 0) {
        ImGui::TextUnformatted(L("Hip shot, scope in, scoped shot after 210 ms, scope out after 50 ms, then three follow-up hip shots, each after a 240 ms wait."));
        ImGui::TextUnformatted(L("A brief release can continue the current stage. A release seen at a checkpoint restarts from shot one on the next press. A shot already waiting may still fire after release."));
        ImGui::TextUnformatted(L("After every shot, vertical tracking pauses for 35 ms while horizontal tracking continues."));
    }
    if (definition.heroId == OW::eHero::HERO_GENJI &&
        definition.inputAction == OW::HeroSkillInputAction::Ability1)
        ImGui::TextUnformatted(L("Dash uses fresh activation samples and a bounded retry interval; kill resets and actual recharge are not yet directly readable."));
    if (std::strcmp(id, "transcendence") == 0)
        ImGui::TextUnformatted(L("Automatic Transcendence is waiting for a verified enemy ultimate activation signal. Enemy charge alone does not trigger it."));
    if (OW::HasHeroSkillControl(definition, OW::HeroSkillControls::PunchAssist)) {
        ImGui::TextUnformatted(L("Hold RMB to charge; press the activation key to aim horizontally and release. Release RMB before the next attempt."));
        ImGui::TextUnformatted(L("Charge timing is estimated from input. Height limits do not check stairs or obstacles."));
        ImGui::TextUnformatted(L("Full charge sets punch strength. Total time includes charging and holding before automatic release; both times are estimates."));
        if (!V2::Output::SupportsMouseMask(V2::Output::ActiveRuntimeSnapshot().descriptor))
            ImGui::TextUnformatted(L("Rocket Punch assist needs mouse masking from the output device."));
    }
    ImGui::TextDisabled("%s / %s", L(OW::HeroSkillCategoryName(definition.category)),
                        L(OW::HeroSkillInputActionName(definition.inputAction)));
    EndSettingTooltip();
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
        const auto action = definition.inputAction;
        if (action == OW::HeroSkillInputAction::Ability1 ||
            action == OW::HeroSkillInputAction::Ability2 ||
            action == OW::HeroSkillInputAction::Ability3 ||
            action == OW::HeroSkillInputAction::Ultimate ||
            action == OW::HeroSkillInputAction::QuickMelee)
            SettingTooltip(L("Keyboard action is written to the game action word for about 55 ms; Box keyboard output is not used."));
    }

    if (hasControl(OW::HeroSkillControls::Enabled)) {
        SettingRow("Enabled");
        changed |= UICheckbox("##skillEnabled", &settings.enabled);
        SkillTooltip(definition);
    }

    if (hasControl(OW::HeroSkillControls::LowAmmoRightClick)) {
        const char* sources[] = { "Estimate From Left Hold", "Read Ammo From Capture" };
        SettingRow("Ammo Source");
        PushControlWidth();
        changed |= UISelect("##zaryaAmmoSource", &settings.zaryaAmmoSource, sources, IM_ARRAYSIZE(sources));
        SettingTooltip(L(settings.zaryaAmmoSource == OW::Config::kZaryaAmmoHudRecognition
            ? "Reads current ammo from the capture card. Waits when the number is unavailable; does not switch to time estimation."
            : "After a completed reload, estimates remaining ammo from how long primary fire is held."));
        ImGui::PopItemWidth();

        SettingRow("Base Threshold (ammo)");
        PushControlWidth();
        changed |= UISlider("##zaryaAmmoThreshold", &settings.zaryaAmmoThreshold, 1.0f, 100.0f, Number(0, ""));
        ImGui::PopItemWidth();

        SettingRow("Random Range (+/- ammo)");
        PushControlWidth();
        changed |= UISlider("##zaryaAmmoJitter", &settings.zaryaAmmoJitter, 0.0f, 99.0f, Number(0, ""));
        ImGui::PopItemWidth();

        const int minimum = (std::max)(1, settings.zaryaAmmoThreshold - settings.zaryaAmmoJitter);
        const int maximum = (std::min)(100, settings.zaryaAmmoThreshold + settings.zaryaAmmoJitter);
        if (BeginSettingTooltip()) {
            if (minimum == maximum)
                ImGui::TextWrapped(L("Fixed threshold: %d ammo. Fires once per magazine while primary fire is held."), minimum);
            else
                ImGui::TextWrapped(L("Chooses one threshold from %d to %d ammo per magazine. Fires once while primary fire is held."), minimum, maximum);
            ImGui::TextUnformatted(L("Threshold changes apply to the next magazine."));
            EndSettingTooltip();
        }
        if (changed) OW::Config::SetHeroSkillSettings(heroId, definition.skillId, settings);
        ImGui::PopID();
        return changed;
    }

    const bool hasSequenceControls = hasControl(OW::HeroSkillControls::SequenceSteps);
    const bool isAsheCombo = definition.heroId == OW::eHero::HERO_ASHE &&
        std::string(definition.skillId ? definition.skillId : "") == "fire-pattern";
    const bool hasTrackingOverlay = hasControl(OW::HeroSkillControls::TrackingOverlay);
    const bool hasComboAction = hasControl(OW::HeroSkillControls::ComboAction);
    const bool hasPitchControls = hasControl(OW::HeroSkillControls::PitchControl);
    const bool hasPhaseTiming = hasControl(OW::HeroSkillControls::PhaseTiming);
    const bool hasHealthThreshold = hasControl(OW::HeroSkillControls::HealthThreshold);
    const bool hasAbsoluteHealth = hasControl(OW::HeroSkillControls::HealthAbsolute);
    const bool hasAbsoluteEnemyHealth = hasControl(OW::HeroSkillControls::EnemyHealthAbsolute);
    const bool hasPunchAssist = hasControl(OW::HeroSkillControls::PunchAssist);
    const bool isDvaMatrix = definition.heroId == OW::eHero::HERO_DVA &&
        std::string(definition.skillId ? definition.skillId : "") == "defense-matrix";
    const bool hasSkillAimDetails = hasTrackingOverlay && !hasPunchAssist && !isDvaMatrix;
    if (hasControl(OW::HeroSkillControls::PausePolicy)) {
        SettingRow("Pause Aim");
        changed |= UICheckbox("##pauseAim", &settings.pauseAim);
        SettingRow("Pause Trigger");
        changed |= UICheckbox("##pauseTrigger", &settings.pauseTrigger);
        SettingRow("Pause Other Skills");
        changed |= UICheckbox("##pauseSkills", &settings.pauseSkills);
        SettingTooltip(L("Pauses assists during the active defensive skill. Rocket Punch pauses while the secondary-fire key is held. Your enabled settings are restored when the state ends."));
        if (changed) OW::Config::SetHeroSkillSettings(heroId, definition.skillId, settings);
        ImGui::PopID();
        return changed;
    }
    if (hasControl(OW::HeroSkillControls::DefensiveConditions)) {
        SettingRow("Low Health Condition");
        changed |= UICheckbox("##healthTrigger", &settings.healthTriggerEnabled);
        SettingRow("Incoming Threat Condition");
        changed |= UICheckbox("##threatTrigger", &settings.threatTriggerEnabled);
        SettingTooltip(L("Ultimate threats are waiting for verified activation signals."));
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kColTextMuted), "%s", L("Normal Skill Threats"));
        for (size_t i = 0; i < OW::SkillConditions::kNormalThreats.size(); ++i) {
            if(!OW::SkillConditions::kNormalThreats[i].incoming) continue;
            bool selected = (settings.normalThreatMask & (1 << i)) != 0;
            ImGui::PushID(static_cast<int>(i));
            SettingRow(OW::SkillConditions::kNormalThreats[i].label);
            if (UICheckbox("##incomingThreat", &selected)) {
                settings.normalThreatMask ^= 1 << i;
                changed = true;
            }
            ImGui::PopID();
        }

    }
    if(definition.automation==OW::SkillAutomation::NormalInterrupt) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kColTextMuted), "%s", L("Normal Skill Threats"));
        for(size_t i=0;i<OW::SkillConditions::kNormalThreats.size();++i) {
            if(OW::SkillConditions::kNormalThreats[i].interruptWindowMs<=0) continue;
            bool selected=(settings.normalThreatMask & (1<<i))!=0;
            ImGui::PushID(static_cast<int>(i));
            SettingRow(OW::SkillConditions::kNormalThreats[i].label);
            if(UICheckbox("##interruptThreat",&selected)) {
                settings.normalThreatMask^=1<<i;changed=true;
            }
            ImGui::PopID();
        }
    }
    if(definition.automation==OW::SkillAutomation::NormalInterrupt ||
       definition.automation==OW::SkillAutomation::LowEnemyHealth) {
        SettingRow("Aim Before Casting");
        changed|=UICheckbox("##autoSkillAim",&settings.automaticAim);
    }
    if (definition.automation == OW::SkillAutomation::ProjectileDefense) {
        SettingRow("Low Health Condition");
        changed |= UICheckbox("##projectileLowHealth",&settings.healthTriggerEnabled);
        SettingRow("Incoming Projectile Condition");
        changed |= UICheckbox("##incomingProjectiles",&settings.projectileThreatEnabled);
        const char* names[]{"Ana Biotic Grenade","Bastion Tactical Grenade"};
        for(int i=0;i<2;++i) {
            bool checked=(settings.projectileThreatMask&(1<<i))!=0;
            ImGui::PushID(i);
            SettingRow(names[i]);
            if(UICheckbox("##projThreat",&checked)) { settings.projectileThreatMask^=1<<i;changed=true; }
            ImGui::PopID();
        }
        if(definition.heroId==OW::eHero::HERO_DVA) {
            SettingRow("Assist Turning");
            changed |= UICheckbox("##matrixTurn",&settings.automaticAim);
            if(settings.automaticAim) {
                SettingRow("Turning Speed");PushControlWidth();
                changed |= UISlider("##matrixTurnSpeed",&settings.tracking.speedScale,1.f,200.f,Number(0," %"));
                ImGui::PopItemWidth();
            }
        }
    }
    if(definition.automation==OW::SkillAutomation::EmreJump) {
        SettingRow("Forward Duration");PushControlWidth();
        changed|=UISlider("##emreMoveMs",&settings.moveForwardMs,50.f,1000.f,Number(0," ms"));ImGui::PopItemWidth();
        SettingRow("Grenade To Movement Delay");PushControlWidth();
        changed|=UISlider("##emreLeadMs",&settings.jumpLeadMs,0.f,500.f,Number(0," ms"));ImGui::PopItemWidth();
    }
    const bool hasSkillOutputKey = !hasSequenceControls && !hasComboAction && !hasPunchAssist &&
        (hasControl(OW::HeroSkillControls::Key) || hasPitchControls ||
         hasPhaseTiming || hasHealthThreshold || hasAbsoluteHealth);

    if (hasControl(OW::HeroSkillControls::Key)) {
        SettingRow("Activation Key");
        PushControlWidth();
        changed |= UIKeyBinding("##skillKey", &settings.key,
                                KeyBindingStorage::AimActivationIndex);
        ImGui::PopItemWidth();
    }


    if ((hasSequenceControls || hasPitchControls || hasPhaseTiming) &&
        !hasControl(OW::HeroSkillControls::Key)) {
        SettingRow("Activation Key");
        PushControlWidth();
        changed |= UIKeyBinding("##skillActivationKey", &settings.key,
                                KeyBindingStorage::AimActivationIndex);
        ImGui::PopItemWidth();
    }

    if (hasControl(OW::HeroSkillControls::Mode)) {
        SettingRow("Mode");
        PushControlWidth();
        changed |= UISelect("##skillMode", &settings.mode, kHeroSkillModes, IM_ARRAYSIZE(kHeroSkillModes));
        ImGui::PopItemWidth();
    }

    if (hasHealthThreshold) {
        SettingRow("Health Threshold");
        PushControlWidth();
        changed |= UISlider("##skillHealth", &settings.healthThreshold, 0.0f, 100.0f, Number(0, "%"));
        ImGui::PopItemWidth();
    }

    if (hasAbsoluteHealth) {
        SettingRow("Current HP Threshold");
        PushControlWidth();
        changed |= UISlider("##skillSelfMaxHealth", &settings.healthThreshold, 0.0f, 500.0f, Number(0, " HP"));
        ImGui::PopItemWidth();
    }

    if (hasControl(OW::HeroSkillControls::MeleeRange) && settings.enabled) {
        const char* modes[] = { "Any target health", "Current health percentage", "Current health amount" };
        SettingRow("Melee Trigger");
        PushControlWidth();
        changed |= UISelect("##meleeHealthMode", &settings.meleeHealthMode, modes, IM_ARRAYSIZE(modes));
        ImGui::PopItemWidth();
        if (settings.meleeHealthMode == OW::Config::kMeleeHealthPercent) {
            SettingRow("Current HP Percentage Threshold");
            PushControlWidth();
            changed |= UISlider("##meleeHealthPercent", &settings.meleeHealthPercent, 0.0f, 100.0f, Number(0, "%"));
            ImGui::PopItemWidth();
            SettingTooltip(L("Uses current total HP divided by maximum total HP. Triggers at or below the selected percentage."));
        }
    }

    if (hasAbsoluteEnemyHealth && (!hasControl(OW::HeroSkillControls::MeleeRange) ||
        (settings.enabled && settings.meleeHealthMode == OW::Config::kMeleeHealthAbsolute))) {
        SettingRow(hasControl(OW::HeroSkillControls::MeleeRange) ? "Current HP Threshold" : "Max Health");
        PushControlWidth();
        changed |= UISlider("##skillEnemyMaxHealth", &settings.enemyHealthThreshold, 0.0f,
                            hasControl(OW::HeroSkillControls::MeleeRange) ? 500.0f : 300.0f, Number(0, " HP"));
        ImGui::PopItemWidth();
    }

    if (hasControl(OW::HeroSkillControls::EnemyHealthThreshold)) {
        SettingRow("Enemy HP Threshold");
        PushControlWidth();
        changed |= UISlider("##skillEnemyHealth", &settings.enemyHealthThreshold, 0.0f, 100.0f, Number(0, "%"));
        ImGui::PopItemWidth();
    }

    if (hasControl(OW::HeroSkillControls::AllyHealthThreshold)) {
        SettingRow("Ally HP Threshold");
        PushControlWidth();
        changed |= UISlider("##skillAllyHealth", &settings.allyHealthThreshold, 0.0f, 100.0f, Number(0, "%"));
        ImGui::PopItemWidth();
    }

    if (hasControl(OW::HeroSkillControls::Distance)) {
        SettingRow(hasAbsoluteEnemyHealth ? "Max Distance" : "Distance");
        PushControlWidth();
        changed |= UISlider("##skillDistance", &settings.distance, 0.0f, hasPunchAssist ? 30.0f : 100.0f,
                            Number(1, " m"));
        ImGui::PopItemWidth();
    }

    if (hasControl(OW::HeroSkillControls::Radius)) {
        SettingRow("Radius");
        PushControlWidth();
        changed |= UISlider("##skillRadius", &settings.radius, 0.0f, 30.0f, Number(1, " m"));
        ImGui::PopItemWidth();
        if (hasControl(OW::HeroSkillControls::GroupCondition))
            SettingTooltip(L("Set the verified effect radius before enabling automatic group healing. A zero radius does not cast."));
    }

    if (hasControl(OW::HeroSkillControls::Prediction)) {
        changed |= UIPredictionAlgorithmSelect("##skillPredictionAlgorithm", &settings.predictionAlgorithm);
        settings.prediction = true;
    }

    if (hasControl(OW::HeroSkillControls::CooldownGuard)) {
        SettingRow("Cooldown Guard");
        changed |= UICheckbox("##skillCooldownGuard", &settings.cooldownGuard);
    }

    if (hasControl(OW::HeroSkillControls::Cooldown)) {
        SettingRow("Cooldown");
        PushControlWidth();
        changed |= UISlider("##skillCooldown", &settings.cooldown, 0.0f, 60.0f, Number(1, " s"));
        ImGui::PopItemWidth();
    }

    if (hasControl(OW::HeroSkillControls::AmmoGuard)) {
        SettingRow("Ammo Guard");
        changed |= UICheckbox("##skillAmmoGuard", &settings.ammoGuard);

        if (settings.ammoGuard) {
            SettingRow("Reserve Ammo");
            PushControlWidth();
            changed |= UISlider("##skillAmmoReserve", &settings.ammoGuardReserve, 0.0f, 12.0f, Number(0, ""));
            ImGui::PopItemWidth();
        }
    }

    if (hasControl(OW::HeroSkillControls::MinTargets)) {
        SettingRow("Min Targets");
        PushControlWidth();
        changed |= UISlider("##skillMinTargets", &settings.minTargets, 1.0f, 8.0f, Number(0, ""));
        ImGui::PopItemWidth();
    }

    if (hasControl(OW::HeroSkillControls::Bone)) {
        SettingRow("Bone");
        PushControlWidth();
        changed |= UISelect("##skillTargetBone", &settings.tracking.bone,
                            kHeroSkillTargetBones, IM_ARRAYSIZE(kHeroSkillTargetBones));
        ImGui::PopItemWidth();
    }

    if (hasControl(OW::HeroSkillControls::MeleeRange)) {
        SettingRow("Melee Range Expansion");
        PushControlWidth();
        changed |= UISlider("##skillMeleeRadius", &settings.meleeRadius, 0.0f, 2.0f, Number(2, " m"));
        ImGui::PopItemWidth();
        SettingTooltip(L("Checks the whole body within Max Distance. Current total HP includes remaining health, armor, shields and extra health. Directly writes the game's melee action for 50 ms; no Box keyboard or activation key is needed."));
    }

    if (hasControl(OW::HeroSkillControls::Hitbox)) {
        SettingRow("Hitbox Scale");
        PushControlWidth();
        changed |= UISlider("##skillTargetHitbox", &settings.tracking.hitbox,
                            OW::Config::kMinHitboxScalePercent,
                            OW::Config::kMaxHitboxScalePercent, Number(0, " %"));
        ImGui::PopItemWidth();
    }

    if ((hasTrackingOverlay || hasPunchAssist) && settings.enabled) {
        const bool canMoveAim=definition.automation==OW::SkillAutomation::None ||
            definition.automation==OW::SkillAutomation::GenjiBlade ||
            (isDvaMatrix && settings.automaticAim) ||
            (settings.automaticAim && (definition.automation==OW::SkillAutomation::LowEnemyHealth ||
                definition.automation==OW::SkillAutomation::NormalInterrupt));
        ImGui::Spacing();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kColTextMuted), "%s", L("Skill Aim"));

        if(canMoveAim) {
        SettingRow("Aim Behavior");
        PushControlWidth();
        if (UISelect("##skillTrackingBehavior", &settings.tracking.aimBehavior,
                     kAimBehavior, IM_ARRAYSIZE(kAimBehavior))) {
            settings.tracking.aimBehavior =
                OW::Config::ClampAimBehaviorIndex(settings.tracking.aimBehavior);
            settings.tracking.aimBehaviorPresetId = -1;
            changed = true;
        }
        ImGui::PopItemWidth();

        SettingRow("Behavior Preset");
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
        SettingRow("Motion method");
        PushControlWidth();
        const auto* selectedTrackingPreset = OW::Config::FindAimBehaviorPreset(settings.tracking.aimBehaviorPresetId);
        int trackingMethod = selectedTrackingPreset ? selectedTrackingPreset->method : settings.tracking.method;
        ImGui::BeginDisabled(settings.tracking.aimBehaviorPresetId >= 0);
        if (UISelect("##skillTrackingMethod", &trackingMethod, kAimMethod, IM_ARRAYSIZE(kAimMethod))) {
            settings.tracking.method = trackingMethod;
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::PopItemWidth();
        static bool koiPresetListFull = false;
        if (isAsheCombo && ImGui::SmallButton(L("Use Koi proportional tracking"))) {
            const int presetId = OW::Config::EnsureKoiTrackingBehaviorPreset();
            koiPresetListFull = presetId < 0;
            if (presetId >= 0) {
                settings.tracking.aimBehaviorPresetId = presetId;
                settings.tracking.aimBehavior = OW::Config::kAimBehaviorTracking;
                settings.tracking.method = 9;
                settings.tracking.smooth = 0.0f;
                settings.tracking.speedScale = 100.0f;
                changed = true;
            }
        }
        if (isAsheCombo && koiPresetListFull)
            SettingTooltip(L("The mode list is full (64). Delete an unused mode to add another."));
        }
        if (hasSkillAimDetails) {
            SettingRow("Priority");
            PushControlWidth();
            const char* priorities[] = {L("FOV"), L("Health"), L("Distance")};
            changed |= UISelect("##skillPriority", &settings.targetPriority, priorities, IM_ARRAYSIZE(priorities));
            ImGui::PopItemWidth();

            SettingRow("Assistance Delay");
            PushControlWidth();
            changed |= UISlider("##skillAssistanceDelay", &settings.assistanceDelayMs, 0.0f, 1000.0f, Number(0, " ms"));
            ImGui::PopItemWidth();
            SettingRow("Require Visible");
            changed |= UICheckbox("##skillRequireVisible", &settings.requireVisible);
            SettingRow("Allow Invisible Sombra");
            changed |= UICheckbox("##skillStealth", &settings.allowInvisibleSombra);
            SettingRow("Target Roles");
            const char* roleNames[] = {L("Damage"), L("Tank"), L("Support")};
            for (int i = 0; i < 3; ++i) {
                bool selected = (settings.targetRoleMask & (1 << i)) != 0;
                ImGui::PushID(i);
                if (UICheckbox(roleNames[i], &selected)) { settings.targetRoleMask ^= 1 << i; changed = true; }
                ImGui::PopID();
                if (i != 2) ImGui::SameLine();
            }
            SettingRow("Draw FOV");
            changed |= UICheckbox("##skillDrawFov", &settings.drawSkillFov);
            ImGui::SameLine();
            changed |= UI::ColorPicker("skillFovColor", settings.skillFovColor);
        }

        if(canMoveAim && !isDvaMatrix) {
        SettingRow("Speed Scale");
        PushControlWidth();
        changed |= UISlider("##skillTrackingSpeedScale", &settings.tracking.speedScale, 0.0f, 100.0f, Number(0, " %"));
        ImGui::PopItemWidth();
        if(hasControl(OW::HeroSkillControls::TrajectoryAction) || definition.automation!=OW::SkillAutomation::None) {
            SettingRow("Horizontal Motion Scale");PushControlWidth();
            changed|=UISlider("##skillYawScale",&settings.tracking.yawScale,0.f,200.f,Number(0," %"));ImGui::PopItemWidth();
            SettingRow("Vertical Motion Scale");PushControlWidth();
            changed|=UISlider("##skillPitchScale",&settings.tracking.pitchScale,0.f,200.f,Number(0," %"));ImGui::PopItemWidth();
        }
        }

        if (!isDvaMatrix) {
            SettingRow("FOV (deg)");
            PushControlWidth();
            changed |= UISlider("##skillTrackingFov", &settings.tracking.fov,
                                OW::Config::kMinFovDeg, OW::Config::kMaxFovDeg, Number(0, " deg"));
            ImGui::PopItemWidth();
        }

        if (!hasPunchAssist && !isDvaMatrix) {
            SettingRow("Bone");
            PushControlWidth();
            changed |= UISelect("##skillTrackingBone", &settings.tracking.bone,
                                kHeroSkillTrackingBones, IM_ARRAYSIZE(kHeroSkillTrackingBones));
            ImGui::PopItemWidth();
        }

        if (!isDvaMatrix) {
            SettingRow("Hitbox Scale");
            PushControlWidth();
            changed |= UISlider("##skillTrackingHitbox", &settings.tracking.hitbox,
                                OW::Config::kMinHitboxScalePercent,
                                OW::Config::kMaxHitboxScalePercent, Number(0, " %"));
            ImGui::PopItemWidth();
        }

        if (hasTrackingOverlay && hasSequenceControls && !isAsheCombo) {
            ImGui::Spacing();
            ImGui::TextColored(
                ImGui::ColorConvertU32ToFloat4(kColTextMuted),
                "%s", L("Shot Phase Tracking"));

            SettingRow("Phase Aware");
            changed |= UICheckbox(
                "##sequencePhaseAwareTracking",
                &settings.sequencePhaseAwareTracking);

            if (settings.sequencePhaseAwareTracking) {
                SettingRow("Post-fire Pause");
                PushControlWidth();
                changed |= UISlider(
                    "##sequencePostFirePause",
                    &settings.sequencePostFirePauseMs,
                    0.0f,
                    500.0f, Number(0, " ms"));
                ImGui::PopItemWidth();

                SettingRow("Recovery");
                PushControlWidth();
                changed |= UISlider(
                    "##sequenceRecovery",
                    &settings.sequenceRecoveryMs,
                    0.0f,
                    1000.0f, Number(0, " ms"));
                ImGui::PopItemWidth();

                SettingRow("Pause X Scale");
                PushControlWidth();
                changed |= UISlider(
                    "##sequencePostFireYawScale",
                    &settings.sequencePostFireYawScale,
                    0.0f,
                    1.0f, Number(2, "x"));
                ImGui::PopItemWidth();

                SettingRow("Pause Y Scale");
                PushControlWidth();
                changed |= UISlider(
                    "##sequencePostFirePitchScale",
                    &settings.sequencePostFirePitchScale,
                    0.0f,
                    1.0f, Number(2, "x"));
                ImGui::PopItemWidth();

                SettingRow("Pre-shot Window");
                PushControlWidth();
                changed |= UISlider(
                    "##sequencePreFireBoostWindow",
                    &settings.sequencePreFireBoostWindowMs,
                    0.0f,
                    500.0f, Number(0, " ms"));
                ImGui::PopItemWidth();

                SettingRow("Pre-shot Boost");
                PushControlWidth();
                changed |= UISlider(
                    "##sequencePreFireBoostScale",
                    &settings.sequencePreFireBoostScale,
                    1.0f,
                    3.0f, Number(2, "x"));
                ImGui::PopItemWidth();
            }
        }

        if (hasSkillAimDetails && hasControl(OW::HeroSkillControls::Prediction)) {
            SettingRow("Prediction Profile");
            ImGui::TextDisabled("skill:%s", definition.skillId ? definition.skillId : "unknown");
            ImGui::SetItemTooltip("%s", L("Edit projectile speed, radius, gravity and cast delay under Prediction Data."));
        }
        if (hasSkillAimDetails && (hasControl(OW::HeroSkillControls::Prediction) || hasControl(OW::HeroSkillControls::TrajectoryAction))) {
            SettingRow("Max Aim Time");
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
        SettingRow(rowLabel);
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
        SettingRow(rowLabel);
        PushControlWidth();
        changed |= UISlider(id, &value, minValue, maxValue, format);
        ImGui::PopItemWidth();
    };

    if (hasPunchAssist) {
        const char* sources[] = { "Infer From Skill Events", "Direct State (Unavailable)" };
        SettingRow("Empowered Punch Source");
        PushControlWidth();
        changed |= UISelect("##punchStateSource", &settings.punchStateSource, sources, IM_ARRAYSIZE(sources));
        SettingTooltip(L(settings.punchStateSource == OW::Config::kPunchStateDirect
            ? "Direct state is unavailable. Uses the normal punch model in this mode."
            : "Estimates the first punch after Meteor Strike. Power Block needs a verified secondary cooldown sample."));
        ImGui::PopItemWidth();
        drawFloatSlider("Max Height Above", "##punchAbove", settings.punchMaxHeightAbove, 0.0f, 5.0f, Number(1, " m"));
        drawFloatSlider("Max Height Below", "##punchBelow", settings.punchMaxHeightBelow, 0.0f, 5.0f, Number(1, " m"));
        drawFloatSlider("Full Charge Time", "##punchCharge", settings.punchFullChargeMs, 200.0f, 2000.0f, Number(0, " ms"));
        drawFloatSlider("Auto-release Total Time", "##punchMaxHold", settings.punchMaxHoldMs, settings.punchFullChargeMs + 100.0f, 5000.0f, Number(0, " ms"));
        drawFloatSlider("Release Lead", "##punchLead", settings.punchReleaseLeadMs, 20.0f, settings.punchMaxHoldMs - 50.0f, Number(0, " ms"));
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
static void DrawHeroSkillCards(bool sequences) {
    const HeroOption& hero = CurrentHeroOption();
    const auto title = [&](const OW::HeroSkillDefinition& definition) {
        return std::string(L(hero.label)) + " / " + L(definition.displayName);
    };
    const auto draw = [&](const OW::HeroSkillDefinition& definition) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - kControlRightPadding);
        DrawHeroSkillDefinition(definition, hero.heroId);
        ImGui::PopTextWrapPos();
    };

    const OW::HeroSkillDefinition* left = nullptr;
    bool rendered = false;
    if (hero.heroId != 0) {
        for (const auto& definition : OW::AllHeroSkillDefinitions()) {
            if (!OW::IsHeroSkillDefinitionActiveForRuntime(definition, hero.heroId))
                continue;
            if (sequences && !OW::HasHeroSkillControl(definition, OW::HeroSkillControls::SequenceSteps) &&
                !OW::HasHeroSkillControl(definition, OW::HeroSkillControls::ComboAction))
                continue;

            rendered = true;
            const bool isAsheCombo = definition.heroId == OW::eHero::HERO_ASHE &&
                definition.skillId && std::strcmp(definition.skillId, "fire-pattern") == 0;
            if (sequences && OW::HasHeroSkillControl(definition, OW::HeroSkillControls::SequenceSteps) && !isAsheCombo) {
                if (left) {
                    UIGroupPair(title(*left).c_str(), [&]() { draw(*left); });
                    left = nullptr;
                }
                UIGroupBox(title(definition).c_str());
                DrawHeroSkillSequenceDefinition(definition, hero.heroId);
                CloseGroupBox();
                continue;
            }
            if (!left) {
                left = &definition;
                continue;
            }
            UIGroupPair(title(*left).c_str(), [&]() { draw(*left); },
                        title(definition).c_str(), [&]() { draw(definition); });
            left = nullptr;
        }
    }
    if (left)
        UIGroupPair(title(*left).c_str(), [&]() { draw(*left); });
    if (!rendered) {
        UIGroupBox(sequences ? "Combo" : "Hero Skills");
        ImGui::TextDisabled("%s", L(sequences ? "No configurable combos" : "No configurable skills"));
        CloseGroupBox();
    }
}

void UI::SkillsPage() {
    ImGui::PushID("SkillsPage");
    DrawHeroSkillCards(false);
    ImGui::PopID();
}

void UI::SequencesPage() {
    ImGui::PushID("SequencesPage");
    DrawHeroSkillCards(true);
    ImGui::PopID();
}

// =====================================================================
// UI::VisualsPage
// =====================================================================
