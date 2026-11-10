#include "ui/FontFile.hpp"
#include <Windows.h>
#include <shellapi.h>

#include "Features/UI.hpp"
#include "ui/WorkspaceShell.hpp"
#include "ui/SlotSelector.hpp"
#include "ui/Text.hpp"
#include "ui/Theme.hpp"
#include "config/UiPreferences.hpp"
#include "ui/DynamicFovEditor.hpp"
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
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ui/EditorSupport.hpp"
#include "ui/AimPlanEditor.hpp"
#include "ui/BehaviorTuningEditor.hpp"
using namespace UI::Theme;

void DrawMiscGeneralPage();
void DrawMiscDiagnosticsPage();
void DrawMiscBehaviorPage();
void DrawMiscMethodPage();
void DrawMiscDynamicFovPage();
void DrawMiscOutputPage();
void DrawMiscScreenPage();

void UI::MiscPage() {
    ImGui::PushID("MiscPage");

    state.miscSubTab = ImClamp(state.miscSubTab, 0, kMiscSubTabCount - 1);
    switch (state.miscSubTab) {
        case 0:
            DrawMiscGeneralPage();
            break;
        case 1:
            DrawMiscDiagnosticsPage();
            break;
        case 2:
            DrawMiscBehaviorPage();
            break;
        case 3:
            DrawMiscMethodPage();
            break;
        case 4:
            DrawMiscDynamicFovPage();
            break;
        case 5:
            DrawMiscOutputPage();
            break;
        case 6:
            DrawMiscScreenPage();
            break;
        default:
            DrawMiscGeneralPage();
            break;
    }

    CloseGroupBox();
    ImGui::Spacing();
    ImGui::TextDisabled("%s", L("Shared presets"));
    if (ImGui::SmallButton(L("Edit behavior preset"))) state.miscSubTab = 2;
    ImGui::SameLine();
    if (ImGui::SmallButton(L("Edit method preset"))) state.miscSubTab = 3;
    ImGui::SameLine();
    if (ImGui::SmallButton(L("Edit FOV preset"))) state.miscSubTab = 4;

    ImGui::PopID();
}

// =====================================================================
// Misc page sections
// =====================================================================
void DrawMiscGeneralPage() {
    UIGroupBox(T(UiText::GroupConfigProfile));
    {
        static char profileName[kConfigProfileNameBufferSize] = "";
        static char newProfileName[kConfigProfileNameBufferSize] = "";
        static bool profileNameInitialized = false;

        std::vector<std::string> profiles = EnumerateConfigProfiles();
        std::vector<const char*> profileItems;
        profileItems.reserve(profiles.size());
        for (const std::string& profile : profiles)
            profileItems.push_back(profile.c_str());

        if (!profileNameInitialized) {
            CopyConfigProfileName(profileName, OW::Config::configFileName);
            profileNameInitialized = true;
        }

        SettingRow(T(UiText::ActiveProfile));
        PushControlWidth();
        int selectedProfileIndex = FindConfigProfileIndex(profiles, OW::Config::configFileName);
        if (UISelect("##configProfile", &selectedProfileIndex,
                     profileItems.data(), static_cast<int>(profileItems.size()), false)) {
            const std::string selectedProfile = profiles[static_cast<size_t>(selectedProfileIndex)];
            if (SelectConfigProfile(selectedProfile.c_str(), profileName))
                SetPersistenceStatus(PersistLastConfigProfile(), T(UiText::SavedAllChanges));
            else
                SetPersistenceStatus(false, "");
        }
        ImGui::PopItemWidth();

        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", T(UiText::SavePolicyNotice));

        SettingRow(T(UiText::Folder));
        {
            char folderBuffer[MAX_PATH] = {};
            const std::string configDirectory = OW::Config::ConfigDirectoryPath();
            std::snprintf(folderBuffer, sizeof(folderBuffer), "%s", configDirectory.c_str());
            const float openButtonWidth = (std::max)(54.0f,ImGui::CalcTextSize(T(UiText::Open)).x+20);
            const float spacing = ImGui::GetStyle().ItemSpacing.x + 10;
            const float folderWidth = MaxFloat(
                120.0f,
                ImGui::GetContentRegionAvail().x - openButtonWidth - spacing);
            ImGui::PushItemWidth(folderWidth);
            ImGui::InputText("##configFolder", folderBuffer, IM_ARRAYSIZE(folderBuffer),
                             ImGuiInputTextFlags_ReadOnly);
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (ImGui::Button(T(UiText::Open), ImVec2(openButtonWidth, ControlHeight())))
                ShellExecuteA(nullptr, "open", configDirectory.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }

        SettingRow(T(UiText::NewProfile));
        {
            const float createButtonWidth = (std::max)(64.0f,ImGui::CalcTextSize(T(UiText::Create)).x+20);
            const float spacing = ImGui::GetStyle().ItemSpacing.x + 10;
            const float inputWidth = MaxFloat(
                90.0f,
                ImGui::GetContentRegionAvail().x - createButtonWidth - spacing);
            ImGui::PushItemWidth(inputWidth);
            ImGui::InputText("##newConfigProfile", newProfileName, IM_ARRAYSIZE(newProfileName));
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (ImGui::Button(T(UiText::Create), ImVec2(createButtonWidth, ControlHeight()))) {
                const std::string normalizedProfile = NormalizeProfileFileName(newProfileName);
                if (CreateConfigProfileFromCurrent(normalizedProfile) &&
                    SelectConfigProfile(normalizedProfile.c_str(), profileName, false)) {
                    const bool remembered = PersistLastConfigProfile();
                    newProfileName[0] = '\0';
                    SetPersistenceStatus(remembered, T(UiText::CreatedConfigProfile));
                } else {
                    SetPersistenceStatus(false, "");
                }
            }
        }
    }
    CloseGroupBox();

    UIGroupBox(T(UiText::GroupMenu));
    {
        SettingRow(T(UiText::Language));
        PushControlWidth();
        int uiLanguage = OW::Config::ClampUiLanguage(OW::Config::uiLanguage);
        if (UISelect("##uiLanguage", &uiLanguage, kUiLanguageOptions, IM_ARRAYSIZE(kUiLanguageOptions)))
        {
            OW::Config::uiLanguage = OW::Config::ClampUiLanguage(uiLanguage);
            UI::Preferences::values.language=OW::Config::uiLanguage;
            UI::Preferences::Save();
        }
        ImGui::PopItemWidth();

        auto& preferences=UI::Preferences::values;
        bool preferenceChanged=false;
        static const char* skins[]={"UN Crimson","Graphite","ED Violet"};
        SettingRow(OW::Config::uiLanguage==1?"界面风格":"Menu style");PushControlWidth();
        if(UISelect("##menuSkin",&preferences.skin,skins,3)) {preferences.customAccent=false;preferenceChanged=true;}
        ImGui::PopItemWidth();
        if(!preferences.customAccent)preferences.accent=ImGui::ColorConvertU32ToFloat4(UI::Theme::kColAccent);
        if(UIColorEdit(OW::Config::uiLanguage==1?"强调色":"Accent color",&preferences.accent)) {preferences.customAccent=true;preferenceChanged=true;}
        SettingRow(OW::Config::uiLanguage==1?"减少动画":"Reduce motion");
        preferenceChanged|=UICheckbox("##reduceMotion",&preferences.reducedMotion);
        SettingRow(OW::Config::uiLanguage==1?"登录毛玻璃":"Login glass");
        preferenceChanged|=UICheckbox("##loginGlass",&preferences.loginGlass);
        SettingRow(OW::Config::uiLanguage==1?"控制台分层":"Console layers");
        preferenceChanged|=UICheckbox("##consoleLayers",&preferences.consoleLayers);
        if(preferenceChanged)UI::Preferences::Save();
        if(!UI::Preferences::Error().empty()) ImGui::TextWrapped("%s",UI::Preferences::Error().c_str());

    }
    CloseGroupBox();

    UIGroupBox(T(UiText::GroupApplication));
    {
        SettingRow(T(UiText::Unleashed));
        if (UIActionButton(T(UiText::CloseUn), 96.0f))
            g_Overlay.RequestExit();
        ImGui::SameLine();
        const bool reconnectBusy = OW::ProcessConnection::IsRecoveryBusy();
        if (reconnectBusy)
            ImGui::BeginDisabled();
        if (UIActionButton(T(UiText::ReconnectUn), 112.0f))
            OW::ProcessConnection::RequestReconnect();
        if (reconnectBusy)
            ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", T(UiText::TooltipReconnect));

        SettingRow(L("Runtime cache"));
        if (reconnectBusy)
            ImGui::BeginDisabled();
        if (UIActionButton(L("Refresh caches"), 144.0f)) {
            StopFirePatternRecorder();
            OW::ProcessConnection::RequestCacheRefresh();
        }
        if (reconnectBusy)
            ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", L("Stops current actions and acquisition, clears entity, camera and VMM caches, then reconnects and collects fresh data."));

        SettingRow("DMA");
        if (reconnectBusy)
            ImGui::BeginDisabled();
        if (UIActionButton(L("Rebuild DMA connection"), 176.0f)) {
            StopFirePatternRecorder();
            OW::ProcessConnection::RequestDmaRecovery();
        }
        if (reconnectBusy)
            ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", L("Stops the current session, reloads DMA device settings, and rebuilds the device connection. Old data and commands are discarded."));

        SettingRow(T(UiText::Overlay));
        if (UIActionButton(T(UiText::MinimizeOverlay), 144.0f))
            g_Overlay.MinimizeToTaskbar();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", T(UiText::TooltipMinimizeOverlay));

        SettingRow(L("Overlay frame limit"));
        PushControlWidth();
        UISlider("##overlayFrameLimit", &OW::Config::overlayFrameLimit, 0, 2000,
                 AtOrBelow(L("Unlimited"), 0, Number(0, " FPS")));
        ImGui::PopItemWidth();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", L("0 = unlimited. Changes apply immediately. Only limits canvas drawing; data acquisition stays at 300 Hz and the menu at about 60 FPS."));

        SettingRow(L("Actual overlay FPS"));
        ImGui::Text("%.0f FPS", Diagnostics::RenderFps());

        SettingRow(T(UiText::RuntimeVersion));
        const auto layout = V2::Profiles::Snapshot();
        const auto* matched = V2::Profiles::MatchedProfile(layout.detection);
        if (OW::ProcessConnection::IsConnected() && matched)
            ImGui::TextWrapped("%s %s", matched->region, matched->build);
        else
            ImGui::TextWrapped("%s", L("No verified active layout. Open Diagnostics to choose or retry."));

        SettingRow(T(UiText::Process));
        const bool connected = OW::ProcessConnection::IsConnected();
        const bool connecting = OW::ProcessConnection::IsConnecting();
        const ImVec4 statusColor = connected
            ? ImVec4(0.25f, 1.0f, 0.45f, 1.0f)
            : connecting
                ? ImVec4(1.0f, 0.75f, 0.25f, 1.0f)
                : ImVec4(1.0f, 0.45f, 0.35f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, statusColor);
        ImGui::TextWrapped("%s", OW::ProcessConnection::StatusText().c_str());
        ImGui::PopStyleColor();
        if (connected) {
            ImGui::TextDisabled("PID %d", OW::ProcessConnection::ConnectedPid());
        }
    }
    CloseGroupBox();
}

void DrawMiscDiagnosticsPage() {
    const auto profile = V2::Profiles::Snapshot();
    const auto* matched = V2::Profiles::MatchedProfile(profile.detection);
    UIGroupBox("版本与布局");
    int selected = V2::Profiles::requestedProfile.load() + 1;
    const char* choices[] = {"Auto", "CN", "World", "Nexon"};
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.44f);
    if (ImGui::Combo("##RequestedProfile", &selected, choices, IM_ARRAYSIZE(choices))) {
        V2::Profiles::requestedProfile.store(selected - 1);
        OW::ProcessConnection::RequestReconnect();
    }
    ImGui::SameLine();
    if (matched)
        ImGui::TextWrapped("已识别 %s · %s", matched->region, matched->build);
    else
        ImGui::TextWrapped("%s", V2::Profiles::StatusText(profile.detection.status));
    if (matched && OW::ProcessConnection::IsConnected()) {
        const auto runtime = Diagnostics::Snapshot();
        ImGui::Text("已发布实体 %zu", runtime.entityCount);
        if (runtime.entityCount == 0)
            ImGui::TextWrapped("等待实体字段完成采集");
        if (!matched->Offsets().VisibilityValueOffset)
            ImGui::TextWrapped("该版本缺少可见性布局");
        if (!matched->Offsets().InputMouseScaleX_RVA)
            ImGui::TextWrapped("该版本缺少游戏鼠标缩放偏移");
    }
    CloseGroupBox();

    DrawRuntimeReadinessDetails();
    UIGroupBox(T(UiText::GroupDiagnostics));
    {
        ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.0f, 1),
            "DMA KeyState reads host Windows VK state (keyboard and mouse)");
        const uint64_t keyStateAddress = KeyState::gafAsyncKeyStateAddr.load();
        const KeyState::ResolverDiagnostics resolver = KeyState::SnapshotResolverDiagnostics();
        if (KeyState::initialized.load() && keyStateAddress != 0) {
            ImGui::TextColored(ImVec4(0.25f, 1.0f, 0.45f, 1),
                "DMA KeyState ready: build=%lu profile=%s method=%s module=%s session=%lu proxyPid=%lu addr=0x%llX size=%zu slotsRva=0x%llX slotsMethod=%s keyOffset=0x%llX keyMethod=%s",
                static_cast<unsigned long>(resolver.build),
                resolver.profile.c_str(),
                resolver.method.c_str(),
                resolver.module.c_str(),
                static_cast<unsigned long>(resolver.resolvedSessionId),
                static_cast<unsigned long>(resolver.proxyPid),
                static_cast<unsigned long long>(keyStateAddress),
                KeyState::keyStateByteCount.load(),
                static_cast<unsigned long long>(resolver.slotsRva),
                resolver.slotsMethod.c_str(),
                static_cast<unsigned long long>(resolver.keyStateOffset),
                resolver.keyOffsetMethod.c_str());
        } else {
            ImGui::TextColored(ImVec4(1, 0.35f, 0.2f, 1),
                "DMA KeyState resolver pending/failed: build=%lu profile=%s method=%s module=%s; check Diagnostic Log",
                static_cast<unsigned long>(resolver.build),
                resolver.profile.c_str(),
                resolver.method.c_str(),
                resolver.module.c_str());
            if (!resolver.lastFailureDetails.empty()) {
                ImGui::TextWrapped("Last resolver failure: %s", resolver.lastFailureDetails.c_str());
            }
        }

        DrawAimHotkeyProbe();
        ImGui::Spacing();

        SettingRow(T(UiText::FireRecorder));
        if (IsFirePatternRecorderRunning()) {
            if (UIActionButton(T(UiText::Stop), 72.0f))
                StopFirePatternRecorder();
        } else {
            if (UIActionButton(T(UiText::Start), 72.0f))
                StartFirePatternRecorder();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", L("Records left/right mouse button down/up edges from DMA KeyState for up to 8 seconds.\nPerform one full fire-pattern cycle plus the first press of the next cycle, then press Stop.\nResults are written to undma_cn_aim_diag.log and the log overlay."));
        }
        ImGui::SameLine();
        bool recorderInterrupted = false;
        const std::string recorderStatus = GetFirePatternRecorderStatus(recorderInterrupted);
        ImGui::PushTextWrapPos();
        ImGui::TextColored(recorderInterrupted ? ImVec4(1.0f, 0.45f, 0.35f, 1.0f)
                                               : ImVec4(0.60f, 0.60f, 0.60f, 1.0f),
                           "%s", L(recorderStatus.c_str()));
        ImGui::PopTextWrapPos();

        ImGui::Spacing();
        ImGui::Checkbox(T(UiText::DryRunLogOnly), &OW::Config::aimDryRun);
        if (OW::Config::aimDryRun) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "%s", T(UiText::NoCursorMovement));
        }
        bool showLogOverlay = Diagnostics::IsLogOverlayVisible();
        if (ImGui::Checkbox(T(UiText::ShowLogOverlay), &showLogOverlay))
            Diagnostics::SetLogOverlayVisible(showLogOverlay);
        ImGui::SameLine();
        if (ImGui::Button(T(UiText::ClearLog)))
            Diagnostics::ClearLogLines();
        ImGui::Checkbox(T(UiText::VerboseAimLogs), &OW::Config::aimVerboseLog);
        if (OW::Config::aimVerboseLog) {
            ImGui::TextWrapped("%s", T(UiText::VerboseLogsWarning));
        }
        SettingRow(T(UiText::LogIntervalMs));
        PushControlWidth();
        UISlider("##LogInterval", &OW::Config::aimDryRunLogIntervalMs, 50.0f, 1000.0f, Number(0, " ms"));
        ImGui::PopItemWidth();
    }
    CloseGroupBox();
}

void DrawMiscBehaviorPage() {
    using namespace OW::Config;
    static int selectedId=View_aimBehaviorPresets().empty()?-1:View_aimBehaviorPresets().front().id,lastId=-2;
    static AimBehaviorPreset draft{};
    static char name[64]="Behavior Preset";
    bool changed=false;
    UIGroupBox("Behavior modes");
    SettingRow("Behavior mode");PushControlWidth();
    if(UISelectAimBehaviorPreset("##modeDefinition",&selectedId,-1,"New mode")) lastId=-2;
    ImGui::PopItemWidth();
    auto* selected=FindMutableAimBehaviorPreset(selectedId);
    if(lastId!=selectedId) {
        draft=selected?*selected:AimBehaviorPreset{};
        CopyPresetNameToBuffer(draft.name,name);lastId=selectedId;
    }
    auto& mode=selected?*selected:draft;
    SettingRow("Preset Name");PushControlWidth();
    if(ImGui::InputText("##modeName",name,IM_ARRAYSIZE(name))) {mode.name=NormalizeUiPresetName(name,"Behavior Preset",-1);changed=true;}
    ImGui::PopItemWidth();
    SettingRow("Aim Behavior");PushControlWidth();
    ImGui::BeginDisabled(mode.defaultBehavior >= 0);
    changed|=UISelect("##modeBehavior",&mode.behavior,kAimBehavior,IM_ARRAYSIZE(kAimBehavior));ImGui::PopItemWidth();
    ImGui::EndDisabled();
    ImGui::TextWrapped("%s",L("Edit mode structure and defaults here. Existing Slot parameters stay independent."));
    changed|=UI::BehaviorTuningEditor(mode,mode.behavior);
    changed|=UI::AimPlanEditor::Motion(mode.motion);
    mode.method=mode.motion.method;mode.methodPresetId=-1;
    const bool modeListFull = std::count_if(View_aimBehaviorPresets().begin(), View_aimBehaviorPresets().end(),
        [](const auto& p) { return p.defaultBehavior < 0; }) >= 64;
    ImGui::BeginDisabled(modeListFull);
    if(UIActionButton(L("Save as new mode"),140.f)) {
        auto created=CaptureBehaviorPresetFromCurrent(mode.behavior,mode.method,-1,mode.baseSpeed,
            mode.startLimiter,mode.moveSplitEnabled,mode.moveSplitMaxPixels,mode.moveSplitDelayUs,name);
        static_cast<AimBehaviorTuning&>(created)=mode;created.motion=mode.motion;
        View_aimBehaviorPresets().push_back(created);selectedId=created.id;lastId=-2;changed=true;
    }
    ImGui::EndDisabled();
    if(modeListFull) ImGui::TextWrapped("%s",L("The mode list is full (64). Delete an unused mode to add another."));
    if(selectedId>=0) {
        const auto* deleteCandidate=FindAimBehaviorPreset(selectedId);
        const bool protectedDefault=deleteCandidate && deleteCandidate->defaultBehavior>=0;
        const bool used=AimBehaviorPresetInUse(selectedId);
        ImGui::SameLine();ImGui::BeginDisabled(used);
        if(UIActionButton(L("Delete mode"),110.f)) {
            std::erase_if(View_aimBehaviorPresets(),[&](const auto& p){return p.id==selectedId && p.defaultBehavior<0;});
            selectedId=-1;lastId=-2;changed=true;
        }
        ImGui::EndDisabled();
        if(protectedDefault) ImGui::TextWrapped("%s",L("Default modes cannot be deleted."));
        else if(used) ImGui::TextWrapped("%s",L("This mode is in use. Choose another mode in its Slots before deleting it."));
    }
    CloseGroupBox();
    UIGroupBox("Output shaping");
    SettingRow(T(UiText::OvershootCurve));UICheckbox("##miscOvershootCurve",&aimOvershootCurve);
    if(aimOvershootCurve) {
        UI::AimPlanEditor::Float("Overshoot Gain","##overshootGain",aimOvershootGain,0,1);
        UI::AimPlanEditor::Float("Overshoot Reset","##overshootReset",aimOvershootResetPixels,1,250," counts");
    }
    CloseGroupBox();
    if(ImGui::CollapsingHeader(OW::Config::uiLanguage==1?"兼容方法库":"Legacy methods"))
        DrawMiscMethodPage();
    if(changed) RefreshBehaviorModeSlots();
}

void DrawMiscMethodPage() {
    UIGroupBox(T(UiText::GroupMethod));
    {
        static int selectedMethod = 0;
        static int selectedMethodPresetId = -1;
        static int lastMethodPresetId = -2;
        static char methodPresetName[64] = "Method Preset";
        selectedMethod = OW::Config::ClampAimMethodIndex(selectedMethod);

        SettingRow(T(UiText::SubMethod));
        PushControlWidth();
        if (UISelect("##miscMethodSelector", &selectedMethod, kAimMethod, IM_ARRAYSIZE(kAimMethod))) {
            if (const OW::Config::AimMethodPreset* selectedPreset =
                    OW::Config::FindAimMethodPreset(selectedMethodPresetId)) {
                if (OW::Config::ClampAimMethodIndex(selectedPreset->method) != selectedMethod)
                    selectedMethodPresetId = -1;
            }
        }
        ImGui::PopItemWidth();

        SettingRow(T(UiText::Preset));
        PushControlWidth();
        if (UISelectAimMethodPreset("##miscMethodPreset", &selectedMethodPresetId, selectedMethod)) {
            if (const OW::Config::AimMethodPreset* selectedPreset =
                    OW::Config::FindAimMethodPreset(selectedMethodPresetId)) {
                selectedMethod = OW::Config::ClampAimMethodIndex(selectedPreset->method);
                CopyPresetNameToBuffer(selectedPreset->name, methodPresetName);
            }
        }
        ImGui::PopItemWidth();

        OW::Config::AimMethodPreset* selectedPreset =
            OW::Config::FindMutableAimMethodPreset(selectedMethodPresetId);
        if (selectedPreset &&
            OW::Config::ClampAimMethodIndex(selectedPreset->method) != selectedMethod) {
            selectedMethodPresetId = -1;
            selectedPreset = nullptr;
        }
        if (lastMethodPresetId != selectedMethodPresetId) {
            if (selectedPreset)
                CopyPresetNameToBuffer(selectedPreset->name, methodPresetName);
            else
                CopyPresetNameToBuffer("Method Preset", methodPresetName);
            lastMethodPresetId = selectedMethodPresetId;
        }

        SettingRow(T(UiText::PresetName));
        PushControlWidth();
        if (ImGui::InputText("##miscMethodPresetName", methodPresetName, IM_ARRAYSIZE(methodPresetName))) {
            if (selectedPreset)
                selectedPreset->name = NormalizeUiPresetName(methodPresetName, "Method Preset", -1);
        }
        ImGui::PopItemWidth();

        selectedMethod = OW::Config::ClampAimMethodIndex(selectedMethod);
        float& angularSpeedScale = selectedPreset
            ? selectedPreset->angularSpeedScale
            : OW::Config::aimMethodAngularSpeedScale[static_cast<size_t>(selectedMethod)];

        auto drawAngularSpeed = [&](const char* id) {
            SettingRow(T(UiText::AngularSpeed));
            PushControlWidth();
            UISlider(id, &angularSpeedScale, 0.0f, 200.0f, Number(0, " %"));
            ImGui::PopItemWidth();
        };

        switch (selectedMethod) {
        case 0:
            drawAngularSpeed("##methodLinearAngularSpeed");
            break;
        case 1:
            SettingRow(T(UiText::PGain));
            PushControlWidth();
            UISlider("##methodPidP", selectedPreset ? &selectedPreset->pidP : &OW::Config::View_aimPidP(), 0.0f, 2.0f, Number(2, ""));
            ImGui::PopItemWidth();

            SettingRow(T(UiText::IGain));
            PushControlWidth();
            UISlider("##methodPidI", selectedPreset ? &selectedPreset->pidI : &OW::Config::View_aimPidI(), 0.0f, 0.5f, Number(3, ""));
            ImGui::PopItemWidth();

            SettingRow(T(UiText::DGain));
            PushControlWidth();
            UISlider("##methodPidD", selectedPreset ? &selectedPreset->pidD : &OW::Config::View_aimPidD(), 0.0f, 1.0f, Number(2, ""));
            ImGui::PopItemWidth();

            SettingRow(T(UiText::MaxIntegral));
            PushControlWidth();
            UISlider("##methodPidMaxI", selectedPreset ? &selectedPreset->pidMaxIntegral : &OW::Config::View_aimPidMaxIntegral(), 1.0f, 50.0f, Number(1, ""));
            ImGui::PopItemWidth();

            SettingRow(T(UiText::Deadzone));
            PushControlWidth();
            UISlider("##methodPidDeadzone", selectedPreset ? &selectedPreset->pidDeadzone : &OW::Config::View_aimPidDeadzone(), 0.0f, 10.0f, Number(1, " deg"));
            ImGui::PopItemWidth();
            break;
        case 6:
            drawAngularSpeed("##methodHybridAngularSpeedScale");

            SettingRow(T(UiText::PGain));
            PushControlWidth();
            UISlider("##methodHybridP", selectedPreset ? &selectedPreset->pidP : &OW::Config::View_aimPidP(), 0.0f, 2.0f, Number(2, ""));
            ImGui::PopItemWidth();

            SettingRow(T(UiText::IGain));
            PushControlWidth();
            UISlider("##methodHybridI", selectedPreset ? &selectedPreset->pidI : &OW::Config::View_aimPidI(), 0.0f, 0.5f, Number(3, ""));
            ImGui::PopItemWidth();

            SettingRow(T(UiText::DGain));
            PushControlWidth();
            UISlider("##methodHybridD", selectedPreset ? &selectedPreset->pidD : &OW::Config::View_aimPidD(), 0.0f, 1.0f, Number(2, ""));
            ImGui::PopItemWidth();

            SettingRow("Deadzone");
            PushControlWidth();
            UISlider("##methodHybridDeadzone", selectedPreset ? &selectedPreset->hybridDeadzoneDeg : &OW::Config::aimHybridDeadzoneDeg, 0.0f, 2.0f, Number(2, " deg"));
            ImGui::PopItemWidth();

            SettingRow("Constant Feed");
            PushControlWidth();
            UISlider("##methodHybridConstant", selectedPreset ? &selectedPreset->hybridConstantSpeedDeg : &OW::Config::aimHybridConstantSpeedDeg, 0.0f, 720.0f, Number(0, " deg/s"));
            ImGui::PopItemWidth();

            SettingRow("Maximum Speed");
            PushControlWidth();
            UISlider("##methodHybridMax", selectedPreset ? &selectedPreset->hybridMaxSpeedDeg : &OW::Config::aimHybridMaxSpeedDeg, 1.0f, OW::Config::kAimConstantAngularSpeedMaxDeg, Number(0, " deg/s"));
            ImGui::PopItemWidth();

            SettingRow("Acceleration");
            PushControlWidth();
            UISlider("##methodHybridAccel", selectedPreset ? &selectedPreset->hybridAccelerationDeg : &OW::Config::aimHybridAccelerationDeg, 1.0f, 10000.0f, Number(0, " deg/s2"));
            ImGui::PopItemWidth();

            SettingRow("Braking");
            PushControlWidth();
            UISlider("##methodHybridDecel", selectedPreset ? &selectedPreset->hybridDecelerationDeg : &OW::Config::aimHybridDecelerationDeg, 1.0f, 10000.0f, Number(0, " deg/s2"));
            ImGui::PopItemWidth();

            SettingRow("Near Radius");
            PushControlWidth();
            UISlider("##methodHybridNear", selectedPreset ? &selectedPreset->hybridNearRadiusDeg : &OW::Config::aimHybridNearRadiusDeg, 0.05f, 15.0f, Number(1, " deg"));
            ImGui::PopItemWidth();

            SettingRow("Target Motion Feed");
            PushControlWidth();
            UISlider("##methodHybridMotion", selectedPreset ? &selectedPreset->hybridTargetMotionGain : &OW::Config::aimHybridTargetMotionGain, 0.0f, 2.0f, Number(2, "x"));
            ImGui::PopItemWidth();

            SettingRow("Sudden Motion Boost");
            PushControlWidth();
            UISlider("##methodHybridBoost", selectedPreset ? &selectedPreset->hybridSuddenMotionBoost : &OW::Config::aimHybridSuddenMotionBoost, 1.0f, 8.0f, Number(1, "x"));
            ImGui::PopItemWidth();
            break;
        case 2:
            SettingRow(T(UiText::ControlPoints));
            PushControlWidth();
            UISlider("##methodBezierControlPoints", selectedPreset ? &selectedPreset->bezierControlPoints : &OW::Config::View_aimBezierControlPoints(), 2.0f, 6.0f, Number(0, ""));
            ImGui::PopItemWidth();

            SettingRow(T(UiText::Curvature));
            PushControlWidth();
            UISlider("##methodBezierCurvature", selectedPreset ? &selectedPreset->bezierCurvature : &OW::Config::View_aimBezierCurvature(), 0.0f, 1.0f, Number(2, ""));
            ImGui::PopItemWidth();

            SettingRow(T(UiText::CurveSpeed));
            PushControlWidth();
            UISlider("##methodBezierSpeed", selectedPreset ? &selectedPreset->bezierSpeed : &OW::Config::View_aimBezierSpeed(), 1.0f, 200.0f, Number(1, ""));
            ImGui::PopItemWidth();
            break;
        case 3: {
            drawAngularSpeed("##methodPiecewiseAngularSpeed");

            SettingRow(T(UiText::NearDegrees));
            PushControlWidth();
            UISlider("##methodPiecewiseNearDegrees", selectedPreset ? &selectedPreset->piecewiseNearDegrees : &OW::Config::aimPiecewiseNearDegrees, 0.0f, 30.0f, Number(1, " deg"));
            ImGui::PopItemWidth();

            float& midDegrees = selectedPreset ? selectedPreset->piecewiseMidDegrees : OW::Config::aimPiecewiseMidDegrees;
            midDegrees = (std::max)(midDegrees, OW::Config::AimPiecewiseNearDegrees(selectedPreset));
            SettingRow(T(UiText::MidDegrees));
            PushControlWidth();
            UISlider("##methodPiecewiseMidDegrees", &midDegrees,
                     OW::Config::AimPiecewiseNearDegrees(selectedPreset), 45.0f, Number(1, " deg"));
            ImGui::PopItemWidth();

            float& farDegrees = selectedPreset ? selectedPreset->piecewiseFarDegrees : OW::Config::aimPiecewiseFarDegrees;
            farDegrees = (std::max)(farDegrees, OW::Config::AimPiecewiseMidDegrees(selectedPreset));
            SettingRow(T(UiText::FarDegrees));
            PushControlWidth();
            UISlider("##methodPiecewiseFarDegrees", &farDegrees,
                     OW::Config::AimPiecewiseMidDegrees(selectedPreset), 60.0f, Number(1, " deg"));
            ImGui::PopItemWidth();

            SettingRow(T(UiText::NearScale));
            PushControlWidth();
            UISlider("##methodPiecewiseNearScale", selectedPreset ? &selectedPreset->piecewiseNearScale : &OW::Config::aimPiecewiseNearScale, 0.0f, 1.0f, Number(2, ""));
            ImGui::PopItemWidth();

            SettingRow(T(UiText::MidScale));
            PushControlWidth();
            UISlider("##methodPiecewiseMidScale", selectedPreset ? &selectedPreset->piecewiseMidScale : &OW::Config::aimPiecewiseMidScale, 0.0f, 1.0f, Number(2, ""));
            ImGui::PopItemWidth();

            SettingRow(T(UiText::FarScale));
            PushControlWidth();
            UISlider("##methodPiecewiseFarScale", selectedPreset ? &selectedPreset->piecewiseFarScale : &OW::Config::aimPiecewiseFarScale, 0.0f, 1.0f, Number(2, ""));
            ImGui::PopItemWidth();
            break;
        }
        case 4:
            drawAngularSpeed("##methodAccelAngularSpeed");

            SettingRow(T(UiText::Acceleration));
            PushControlWidth();
            UISlider("##methodAccelAcceleration", selectedPreset ? &selectedPreset->accelLimitedAcceleration : &OW::Config::aimAccelLimitedAcceleration, 0.0f, 20.0f, Number(2, ""));
            ImGui::PopItemWidth();
            break;
        case 5:
            SettingRow(T(UiText::AngularSpeed));
            PushControlWidth();
            UISlider("##methodConstantAngularSpeed",
                     selectedPreset ? &selectedPreset->constantAngularSpeedDeg : &OW::Config::aimConstantAngularSpeedDeg,
                     0.0f, OW::Config::kAimConstantAngularSpeedMaxDeg, Number(0, " deg/s"));
            ImGui::PopItemWidth();
            break;
        case 7: {
            drawAngularSpeed("##methodShuwuAngularSpeed");
            koi::ShuwuConfig defaults{};
            if (!selectedPreset)
                ImGui::TextWrapped("%s", L("Save a new method preset to edit Shuwu parameters."));
            ImGui::BeginDisabled(!selectedPreset);
            UI::AimPlanEditor::Shuwu(selectedPreset ? selectedPreset->shuwu : defaults);
            ImGui::EndDisabled();
            break;
        }
        case 8: {
            drawAngularSpeed("##methodResponseAngularSpeed");
            OW::AimResponseCurve::Points defaults;
            if(!selectedPreset) ImGui::TextWrapped("%s",L("Save a new method preset to edit response curve parameters."));
            ImGui::BeginDisabled(!selectedPreset);
            UI::ResponseCurveEditor(selectedPreset?selectedPreset->responseCurve:defaults);
            ImGui::EndDisabled();break;
        }
        default:
            break;
        }

        if (selectedPreset) {
            selectedPreset->method = selectedMethod;
            selectedPreset->name = NormalizeUiPresetName(methodPresetName, "Method Preset", -1);
        }

        ImGui::Spacing();
        if (UIActionButton(T(UiText::SaveNew), 92.0f)) {
            OW::Config::AimMethodPreset preset =
                selectedPreset ? *selectedPreset : CaptureMethodPresetFromCurrent(selectedMethod, methodPresetName);
            preset.id = NextAimMethodPresetId();
            preset.name = NormalizeUiPresetName(methodPresetName, "Method Preset", static_cast<int>(OW::Config::View_aimMethodPresets().size()));
            preset.method = selectedMethod;
            OW::Config::View_aimMethodPresets().push_back(preset);
            selectedMethodPresetId = preset.id;
            lastMethodPresetId = -2;
            selectedPreset = nullptr;
        }
        if (selectedPreset) {
            ImGui::SameLine(0.0f, 8.0f);
            if (UIActionButton(T(UiText::Delete), 72.0f)) {
                const int deletedId = selectedMethodPresetId;
                OW::Config::View_aimMethodPresets().erase(
                    std::remove_if(OW::Config::View_aimMethodPresets().begin(),
                                   OW::Config::View_aimMethodPresets().end(),
                                   [deletedId](const OW::Config::AimMethodPreset& preset) {
                                       return preset.id == deletedId;
                                   }),
                    OW::Config::View_aimMethodPresets().end());
                for (int& methodPresetId : OW::Config::View_aimBehaviorMethodPreset()) {
                    if (methodPresetId == deletedId)
                        methodPresetId = -1;
                }
                for (OW::Config::AimBehaviorPreset& behaviorPreset : OW::Config::View_aimBehaviorPresets()) {
                    if (behaviorPreset.methodPresetId == deletedId)
                        behaviorPreset.methodPresetId = -1;
                }
                selectedMethodPresetId = -1;
                lastMethodPresetId = -2;
            }
        }
    }
    CloseGroupBox();
}

void DrawMiscDynamicFovPage() {
    UIGroupBox("Dynamic FOV Presets");
    {
        static int selectedPresetId = -1;
        static int lastPresetId = -2;
        static char presetName[64] = "Dynamic FOV";
        static OW::Config::DynamicFovPreset draft{};

        OW::Config::DynamicFovPreset* selectedPreset =
            OW::Config::FindMutableDynamicFovPreset(selectedPresetId);
        if (lastPresetId != selectedPresetId) {
            if (selectedPreset) {
                CopyPresetNameToBuffer(selectedPreset->name, presetName);
            } else {
                draft = OW::Config::DynamicFovPreset{};
                CopyPresetNameToBuffer("Dynamic FOV", presetName);
            }
            lastPresetId = selectedPresetId;
        }

        SettingRow("Preset");
        PushControlWidth();
        if (UISelectDynamicFovPreset("##miscDynamicFovPreset", &selectedPresetId, true)) {
            selectedPreset = OW::Config::FindMutableDynamicFovPreset(selectedPresetId);
            if (selectedPreset) {
                CopyPresetNameToBuffer(selectedPreset->name, presetName);
            } else {
                draft = OW::Config::DynamicFovPreset{};
                CopyPresetNameToBuffer("Dynamic FOV", presetName);
            }
            lastPresetId = selectedPresetId;
        }
        ImGui::PopItemWidth();

        selectedPreset = OW::Config::FindMutableDynamicFovPreset(selectedPresetId);
        OW::Config::DynamicFovPreset& editPreset = selectedPreset ? *selectedPreset : draft;

        SettingRow("Preset Name");
        PushControlWidth();
        if (ImGui::InputText("##miscDynamicFovName", presetName, IM_ARRAYSIZE(presetName)))
            editPreset.name = NormalizeUiPresetName(presetName, "Dynamic FOV", -1);
        ImGui::PopItemWidth();

        SettingRow("Smooth Curve");
        UICheckbox("##miscDynamicFovSmooth", &editPreset.smooth);

        UI::DynamicFovEditor(editPreset);

        editPreset = OW::Config::ValidateDynamicFovPresetValue(editPreset);
        if (selectedPreset)
            CopyPresetNameToBuffer(selectedPreset->name, presetName);

        const float preview5 = OW::Config::EvaluateDynamicFovPreset(editPreset, 5.0f, OW::Config::kDefaultFovDeg);
        const float preview10 = OW::Config::EvaluateDynamicFovPreset(editPreset, 10.0f, OW::Config::kDefaultFovDeg);
        const float preview20 = OW::Config::EvaluateDynamicFovPreset(editPreset, 20.0f, OW::Config::kDefaultFovDeg);
        const float preview30 = OW::Config::EvaluateDynamicFovPreset(editPreset, 30.0f, OW::Config::kDefaultFovDeg);
        char preview[128] = {};
        std::snprintf(preview,
                      sizeof(preview),
                      "5m %.0f deg   10m %.0f deg   20m %.0f deg   30m %.0f deg",
                      preview5,
                      preview10,
                      preview20,
                      preview30);
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.78f, 0.84f, 0.92f, 1.0f), "%s", preview);

        ImGui::Spacing();
        if (UIActionButton(T(UiText::SaveNew), 92.0f)) {
            OW::Config::DynamicFovPreset preset = editPreset;
            preset.id = NextDynamicFovPresetId();
            preset.name = NormalizeUiPresetName(
                presetName,
                "Dynamic FOV",
                static_cast<int>(OW::Config::View_dynamicFovPresets().size()));
            preset = OW::Config::ValidateDynamicFovPresetValue(preset);
            OW::Config::View_dynamicFovPresets().push_back(preset);
            selectedPresetId = preset.id;
            lastPresetId = -2;
        }

        if (selectedPreset) {
            ImGui::SameLine(0.0f, 8.0f);
            if (UIActionButton(T(UiText::Delete), 72.0f)) {
                const int deletedId = selectedPresetId;
                OW::Config::View_dynamicFovPresets().erase(
                    std::remove_if(OW::Config::View_dynamicFovPresets().begin(),
                                   OW::Config::View_dynamicFovPresets().end(),
                                   [deletedId](const OW::Config::DynamicFovPreset& preset) {
                                       return preset.id == deletedId;
                                   }),
                    OW::Config::View_dynamicFovPresets().end());
                if (OW::Config::View_aimbotDynamicFovPresetId() == deletedId) {
                    OW::Config::View_aimbotFovMode() = OW::Config::kFovModeFixed;
                    OW::Config::View_aimbotDynamicFovPresetId() = -1;
                }
                for (auto& heroSlots : OW::Config::View_heroAimPresets()) {
                    for (OW::Config::HeroSlotPreset& slot : heroSlots.second) {
                        if (slot.preset.dynamicFovPresetId == deletedId) {
                            slot.preset.fovMode = OW::Config::kFovModeFixed;
                            slot.preset.dynamicFovPresetId = -1;
                        }
                    }
                }
                selectedPresetId = -1;
                lastPresetId = -2;
            }
        }
    }
    CloseGroupBox();
}

void DrawMiscOutputPage() {
    const auto device=V2::Output::Describe(V2::Output::ActiveRuntimeSnapshot().descriptor);
    ImGui::TextWrapped("%s · 移动 %s / 鼠标 %s / 键盘 %s / 物理监测 %s",
        device.name,device.move?"可用":"不支持",device.buttons?"可用":"不支持",
        device.keyboard?"可用":"不支持",device.physicalMonitor?"可用":"不支持");
    ImGui::TextWrapped("%s", L("Device rate is not measured in this build."));
    if (OW::Config::outputDeviceType == 5)
        ImGui::TextWrapped("Ferrum：释放会强制松开按键约 125–175 ms 后恢复物理状态；连接经 Ferrum App，实机待验证。");
    ImGui::TextWrapped("%s", L("MAKCU Controller / Medius: unavailable until their action mapping and protocol are established."));
    ImGui::Spacing();

    const auto physical=V2::Output::ActivePhysicalInput();
    if(OW::Config::outputDeviceType>=3) ImGui::TextWrapped("软件实现已接入；实机待验证。状态监听：%s",physical.available?"当前有效":"暂无有效样本");
    UIGroupBox(T(UiText::GroupOutputSettings));
    {
        bool outputRuntimeReinitRequested = false;
        static bool outputConnectionTestOk = false;
        static std::string outputConnectionTestMessage;
        static bool outputNetworkRestartOk = false;
        static std::string outputNetworkRestartMessage;
        static bool outputFirewallOk = false;
        static std::string outputFirewallMessage;
        static bool gameSensitivityReadOk = false;
        static std::string gameSensitivityReadMessage;
        auto markOutputRuntimeConfigChanged = [&]() {
            outputConnectionTestMessage.clear();
            outputNetworkRestartMessage.clear();
            outputFirewallMessage.clear();
            if (OW::Config::outputEnabled)
                outputRuntimeReinitRequested = true;
        };
        ImGui::PushID("OutputSettings");

        SettingRow(T(UiText::EnableOutput));
        const bool wasOutputEnabled = OW::Config::outputEnabled;
        if (ImGui::Checkbox("##Enable", &OW::Config::outputEnabled)) {
            outputNetworkRestartMessage.clear();
            outputFirewallMessage.clear();
            if (OW::Config::outputEnabled && !wasOutputEnabled) {
                const OutputConnectionTestResult initResult =
                    ApplyOutputRuntimeFromCurrentConfig();
                outputConnectionTestOk = initResult.ok;
                outputConnectionTestMessage = initResult.message;
            } else if (!OW::Config::outputEnabled && wasOutputEnabled) {
                const OutputConnectionTestResult disableResult =
                    ApplyOutputRuntimeFromCurrentConfig();
                outputConnectionTestOk = disableResult.ok;
                outputConnectionTestMessage = disableResult.ok
                    ? T(UiText::Disabled)
                    : disableResult.message;
            }
        }
        if (!outputConnectionTestMessage.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(
                outputConnectionTestOk ? ImVec4(0.30f, 0.90f, 0.45f, 1.0f)
                                      : ImVec4(1.00f, 0.28f, 0.28f, 1.0f),
                "%s",
                outputConnectionTestMessage.c_str());
        }

        SettingRow(T(UiText::DeviceType));
        PushControlWidth();
        if (UISelect("##DeviceType", &OW::Config::outputDeviceType,
                         kOutputDeviceTypes, IM_ARRAYSIZE(kOutputDeviceTypes))) {
            markOutputRuntimeConfigChanged();
        }
        ImGui::PopItemWidth();

        if (OW::Config::outputDeviceType == 0) {
            SettingRow(T(UiText::Ip));
            PushControlWidth();
            ImGui::InputText("##Ip", OW::Config::outputIp, IM_ARRAYSIZE(OW::Config::outputIp));
            if (ImGui::IsItemDeactivatedAfterEdit())
                markOutputRuntimeConfigChanged();
            ImGui::PopItemWidth();

            SettingRow(T(UiText::Port));
            PushControlWidth();
            ImGui::InputInt("##Port", &OW::Config::outputPort, 0, 0);
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                OW::Config::NormalizeOutputPorts();
                markOutputRuntimeConfigChanged();
            }
            ImGui::PopItemWidth();

            SettingRow(T(UiText::MonitorPort));
            PushControlWidth();
            int effectiveMonitorPort = OW::Config::EffectiveOutputMonitorPort();
            ImGui::InputInt(
                "##MonitorPort",
                &effectiveMonitorPort,
                0,
                0,
                ImGuiInputTextFlags_ReadOnly);
            ImGui::PopItemWidth();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", L("Effective KMBox monitor UDP port. Auto mode keeps this synced to the recommended port."));

            if (ImGui::TreeNodeEx((std::string(L("Advanced")) + "###OutputMonitorPort").c_str())) {
                SettingRow("Manual Monitor Port");
                if (UICheckbox("##MonitorPortManualOverride", &OW::Config::outputMonitorPortManualOverride)) {
                    OW::Config::NormalizeOutputPorts();
                    markOutputRuntimeConfigChanged();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", L("Advanced override. Auto mode uses the recommended monitor port derived from the command port."));

                if (OW::Config::outputMonitorPortManualOverride) {
                    SettingRow(T(UiText::MonitorPort));
                    PushControlWidth();
                    ImGui::InputInt("##ManualMonitorPort", &OW::Config::outputMonitorPort, 0, 0);
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        OW::Config::NormalizeOutputPorts();
                        markOutputRuntimeConfigChanged();
                    }
                    ImGui::PopItemWidth();
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", L("Manual monitor UDP port. It will be repaired if it matches the command port."));
                }
                ImGui::TreePop();
            }

            SettingRow(T(UiText::Mac));
            PushControlWidth();
            ImGui::InputText("##Mac", OW::Config::outputMac, IM_ARRAYSIZE(OW::Config::outputMac));
            if (ImGui::IsItemDeactivatedAfterEdit())
                markOutputRuntimeConfigChanged();
            ImGui::PopItemWidth();
        } else if (OW::Config::outputDeviceType == 6) {
            SettingRow("CatBox IP"); ImGui::InputText("##CatIp",OW::Config::catBoxIp,IM_ARRAYSIZE(OW::Config::catBoxIp));
            if(ImGui::IsItemDeactivatedAfterEdit()) markOutputRuntimeConfigChanged();
            SettingRow("CatBox 端口"); ImGui::InputInt("##CatPort",&OW::Config::catBoxPort);
            if(ImGui::IsItemDeactivatedAfterEdit()) markOutputRuntimeConfigChanged();
            SettingRow("CatBox UUID"); ImGui::InputText("##CatUuid",OW::Config::catBoxUuid,IM_ARRAYSIZE(OW::Config::catBoxUuid));
            if(ImGui::IsItemDeactivatedAfterEdit()) markOutputRuntimeConfigChanged();
            SettingRow("CatBox 监听端口"); ImGui::InputInt("##CatMonitor",&OW::Config::catBoxMonitorPort);
            if(ImGui::IsItemDeactivatedAfterEdit()) markOutputRuntimeConfigChanged();
        } else if (OW::Config::outputDeviceType == 1 || OW::Config::outputDeviceType >= 3) {
            SettingRow(T(UiText::ComPort));
            PushControlWidth();
            ImGui::InputText("##ComPort", OW::Config::outputComPort, IM_ARRAYSIZE(OW::Config::outputComPort));
            if (ImGui::IsItemDeactivatedAfterEdit())
                markOutputRuntimeConfigChanged();
            ImGui::PopItemWidth();
        } else {
            const V2::Output::MockHardwareSnapshot snapshot = V2::Output::MockHardwareMgr.Snapshot();
            SettingRow(T(UiText::MockStatus));
            ImGui::TextColored(
                snapshot.initialized ? ImVec4(0.30f, 0.90f, 0.45f, 1.0f)
                                     : ImVec4(0.85f, 0.65f, 0.25f, 1.0f),
                "%s",
                snapshot.initialized ? T(UiText::Ready) : T(UiText::NotInitialized));
        }

        SettingRow(T(UiText::BaseCountsRad));
        PushControlWidth();
        ImGui::InputFloat("##CountsPerRadian", &OW::Config::outputCountsPerRadian,
                          1.0f, 10.0f, "%.1f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", L("Manual device relative mouse counts per radian. Calibration overrides this value until calibration is cleared."));
        ImGui::PopItemWidth();

        SettingRow(T(UiText::CurrentGameSens));
        PushControlWidth();
        ImGui::InputFloat("##GameSens", &OW::Config::gameMouseSensitivity,
                          0.0f, 0.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", L("Manual current in-game sensitivity. Auto-read uses a separate runtime value and falls back to this when unavailable."));
        ImGui::PopItemWidth();

        SettingRow(T(UiText::AutoReadGameSens));
        if (UICheckbox("##AutoReadGameSens", &OW::Config::autoReadGameMouseSensitivity)) {
            gameSensitivityReadMessage.clear();
            if (OW::Config::autoReadGameMouseSensitivity) {
                gameSensitivityReadOk = OW::RefreshAutoGameMouseSensitivity(true);
                char buffer[96] = {};
                std::snprintf(buffer, sizeof(buffer),
                    gameSensitivityReadOk ? "live %.2f" : "not available",
                    OW::Config::detectedGameMouseSensitivity);
                gameSensitivityReadMessage = buffer;
            } else {
                OW::Config::detectedGameMouseSensitivity = 0.0f;
                OW::Config::gameMouseSensitivityAutoDetected = false;
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", L("Reads current in-game sensitivity from the live singleton path when available, with input.MouseScaleX/Y as the World fallback. Manual sensitivity remains the final fallback."));
        ImGui::SameLine();
        if (UIActionButton(T(UiText::ReadGameSens), 64.0f)) {
            float detectedSensitivity = 0.0f;
            uint64_t sourceObject = 0;
            gameSensitivityReadOk = OW::TryReadGameMouseSensitivity(detectedSensitivity, &sourceObject);
            char buffer[128] = {};
            if (gameSensitivityReadOk) {
                if (OW::Config::autoReadGameMouseSensitivity) {
                    OW::Config::detectedGameMouseSensitivity = detectedSensitivity;
                    OW::Config::gameMouseSensitivityAutoDetected = true;
                } else {
                    OW::Config::gameMouseSensitivity = detectedSensitivity;
                    OW::Config::gameMouseSensitivityAutoDetected = false;
                    OW::Config::detectedGameMouseSensitivity = 0.0f;
                }
                std::snprintf(buffer, sizeof(buffer), "read %.2f", detectedSensitivity);
                Diagnostics::Aim("game_sens.manual_read value=%.6f object=0x%llX autoRead=%d",
                    detectedSensitivity,
                    static_cast<unsigned long long>(sourceObject),
                    OW::Config::autoReadGameMouseSensitivity ? 1 : 0);
            } else {
                std::snprintf(buffer, sizeof(buffer), "read failed");
            }
            gameSensitivityReadMessage = buffer;
        }
        if (!gameSensitivityReadMessage.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(
                gameSensitivityReadOk ? ImVec4(0.30f, 0.90f, 0.45f, 1.0f)
                                      : ImVec4(1.00f, 0.28f, 0.28f, 1.0f),
                "%s",
                gameSensitivityReadMessage.c_str());
        }
        if (OW::Config::autoReadGameMouseSensitivity)
            OW::RefreshAutoGameMouseSensitivity();

        SettingRow(T(UiText::EffectiveGameSens));
        PushControlWidth();
        float effectiveGameSensitivity = OW::Config::EffectiveGameMouseSensitivity();
        ImGui::InputFloat("##EffectiveGameSens", &effectiveGameSensitivity,
                          0.0f, 0.0f, "%.2f",
                          ImGuiInputTextFlags_ReadOnly);
        ImGui::PopItemWidth();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", L("The sensitivity value currently used by Auto Sens Scale. It is DMA-read when auto-read succeeds, otherwise manual."));

        SettingRow(T(UiText::HostDpiDma));
        PushControlWidth();
        float detectedHostDpi = OW::Config::hostMouseDpiAutoDetected
            ? OW::Config::detectedHostMouseDpi
            : 0.0f;
        ImGui::InputFloat("##HostDpiDisplay", &detectedHostDpi, 0.0f, 0.0f, "%.0f",
                          ImGuiInputTextFlags_ReadOnly);
        ImGui::PopItemWidth();

        SettingRow(T(UiText::MouseDpi));
        PushControlWidth();
        ImGui::InputFloat("##MouseDpi", &OW::Config::hostMouseDpi,
                          0.0f, 0.0f, "%.0f");
        ImGui::PopItemWidth();

        SettingRow(T(UiText::ReferenceGameSens));
        const float useCurrentButtonWidth = 88.0f;
        const float useCurrentSpacing = ImGui::GetStyle().ItemInnerSpacing.x;
        const float referenceWidth = MaxFloat(
            80.0f,
            ImGui::GetContentRegionAvail().x - useCurrentButtonWidth - useCurrentSpacing);
        ImGui::PushItemWidth(referenceWidth);
        ImGui::InputFloat("##ReferenceGameSens", &OW::Config::referenceGameSensitivity,
                          0.0f, 0.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", L("The in-game sensitivity used when Base Counts/Rad or calibration was measured."));
        ImGui::PopItemWidth();
        ImGui::SameLine();
        if (ImGui::Button(T(UiText::UseCurrent), ImVec2(useCurrentButtonWidth, ControlHeight()))) {
            OW::Config::referenceGameSensitivity = OW::Config::EffectiveGameMouseSensitivity();
        }

        SettingRow(T(UiText::AutoSensScale));
        UICheckbox("##AutoScaleGameSens", &OW::Config::autoScaleByGameSensitivity);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", L("Scales device counts by reference game sensitivity divided by current game sensitivity."));

        SettingRow(T(UiText::InputDelayMs));
        PushControlWidth();
        UISlider("##InputDelay", &OW::Config::outputInputDelayMs,
                                       0.0f, 20.0f, Number(0, " ms"));
        ImGui::PopItemWidth();

        SettingRow(T(UiText::BlockOutputWhenMenuOpen));
        bool suppressOutputWhileMenuOpen =
            OW::Config::outputSuppressOutputWhileMenuOpen.load(
                std::memory_order_acquire);
        if (UICheckbox(
                "##SuppressOutputWhileMenuOpen",
                &suppressOutputWhileMenuOpen)) {
            OW::Config::outputSuppressOutputWhileMenuOpen.store(
                suppressOutputWhileMenuOpen,
                std::memory_order_release);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", L("When enabled, device mouse and keyboard output commands are skipped while this menu is open."));

        SettingRow(T(UiText::ConnectionTest));
        if (UIActionButton(T(UiText::Test), 72.0f)) {
            const OutputConnectionTestResult testResult = TestOutputConnection();
            outputConnectionTestOk = testResult.ok;
            outputConnectionTestMessage = testResult.message;
        }
        if (!outputConnectionTestMessage.empty()) {
            ImGui::PushTextWrapPos();
            ImGui::TextColored(
                outputConnectionTestOk ? ImVec4(0.30f, 0.90f, 0.45f, 1.0f)
                                      : ImVec4(1.00f, 0.28f, 0.28f, 1.0f),
                "%s",
                outputConnectionTestMessage.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::Spacing();
        if (ImGui::CollapsingHeader(L("Advanced device tools"))) {
        SettingRow(T(UiText::DebugLogging));
        ImGui::Checkbox("##Debug", &OW::Config::outputDebugLog);

        if (OW::Config::outputDeviceType == 2) {
            V2::Output::MockHardwareSnapshot snapshot = V2::Output::MockHardwareMgr.Snapshot();

            SettingRow(T(UiText::MockFault));
            PushControlWidth();
            int faultMode = static_cast<int>(snapshot.faultMode);
            if (UISelect("##MockFaultMode", &faultMode, kMockFaultModes, IM_ARRAYSIZE(kMockFaultModes))) {
                faultMode = std::clamp(faultMode, 0, IM_ARRAYSIZE(kMockFaultModes) - 1);
                V2::Output::MockHardwareMgr.SetFaultMode(static_cast<V2::Output::MockFaultMode>(faultMode));
                snapshot = V2::Output::MockHardwareMgr.Snapshot();
            }
            ImGui::PopItemWidth();

            SettingRow(T(UiText::MockReset));
            if (UIActionButton(T(UiText::Reset), 72.0f))
                V2::Output::MockHardwareMgr.Reset();

            snapshot = V2::Output::MockHardwareMgr.Snapshot();
            SettingRow(T(UiText::MockStats));
            ImGui::Text("events=%llu moves=%llu buttons=%llu keys=%llu masks=%llu out=0x%02X masked=0x%02X",
                snapshot.totalEvents,
                snapshot.moveEvents,
                snapshot.buttonEvents,
                snapshot.keyboardEvents,
                snapshot.maskEvents,
                snapshot.outputMouseButtons,
                snapshot.maskedButtons);
        }

        // ---- Counts-per-radian auto-calibration button ----
        SettingRow(T(UiText::CountsCalibration));
        {
            static std::string calibrationStatus;
            static bool calibrationFailed = false;
            const bool wasCalibrated = OW::Config::calibratedCountsPerRadian > 0.0f;
            if (ImGui::Button(OW::Config::calibrationInProgress ? T(UiText::Calibrating) : T(UiText::Calibrate),
                              ImVec2(72.0f, ControlHeight()))) {
                if (!OW::Config::calibrationInProgress) {
                    const float result = OW::CalibrateSensitivity();
                    calibrationFailed = !(result > 0.0f);
                    calibrationStatus = L(calibrationFailed
                        ? "Calibration failed. Previous values were kept; check connection, camera, output device and output pause state."
                        : "Calibration completed. Settings will save automatically after 4 seconds.");
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", L("Sends a known device movement and measures the view angle change to compute actual counts per radian.\nRequires the game to be running and in a state where mouse movement changes view angles.\nWarning: This will move the remote machine's mouse cursor!"));
            ImGui::SameLine();
            if (wasCalibrated) {
                char buf[128];
                std::snprintf(buf, sizeof(buf), "Yaw: %.1f c/rad @ %.2f",
                    OW::Config::calibratedCountsPerRadian,
                    OW::Config::referenceGameSensitivity);
                if (OW::Config::calibratedPitchCountsPerRadian > 0.0f) {
                    std::snprintf(buf, sizeof(buf), "Yaw: %.1f / Pitch: %.1f c/rad @ %.2f",
                        OW::Config::calibratedCountsPerRadian,
                        OW::Config::calibratedPitchCountsPerRadian,
                        OW::Config::referenceGameSensitivity);
                }
                ImGui::TextColored(ImVec4(0.30f, 0.90f, 0.45f, 1.0f), "%s", buf);
            } else {
                ImGui::TextColored(ImVec4(0.60f, 0.60f, 0.60f, 1.0f), "%s", T(UiText::NotCalibrated));
            }
            if (!calibrationStatus.empty())
                ImGui::TextColored(calibrationFailed ? ImVec4(1.0f, 0.45f, 0.35f, 1.0f)
                                                     : ImVec4(0.30f, 0.90f, 0.45f, 1.0f),
                                   "%s", calibrationStatus.c_str());
        }

        if (OW::Config::outputDeviceType == 0) {
            SettingRow(T(UiText::RestartNic));
            if (UIActionButton(T(UiText::RestartNic), 104.0f)) {
                const OutputConnectionTestResult restartResult = RestartOutputNetworkAdapter();
                outputNetworkRestartOk = restartResult.ok;
                outputNetworkRestartMessage = restartResult.message;
            }
        }
        if (!outputNetworkRestartMessage.empty()) {
            ImGui::TextColored(
                outputNetworkRestartOk ? ImVec4(0.30f, 0.90f, 0.45f, 1.0f)
                                      : ImVec4(1.00f, 0.28f, 0.28f, 1.0f),
                "%s",
                outputNetworkRestartMessage.c_str());
        }
        if (OW::Config::outputDeviceType == 0) {
            SettingRow(T(UiText::MonitorFirewall));
            if (UIActionButton(T(UiText::FixFirewall), 112.0f)) {
                const OutputConnectionTestResult firewallResult = AllowOutputMonitorFirewall();
                outputFirewallOk = firewallResult.ok;
                outputFirewallMessage = firewallResult.message;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", L("Adds an elevated inbound UDP allow rule for this executable and disables matching block rules on the KMBox monitor port."));
            if (!outputFirewallMessage.empty()) {
                ImGui::SameLine();
                ImGui::TextColored(
                    outputFirewallOk ? ImVec4(0.30f, 0.90f, 0.45f, 1.0f)
                                    : ImVec4(1.00f, 0.28f, 0.28f, 1.0f),
                    "%s",
                    outputFirewallMessage.c_str());
            }
        }

        }

        if (outputRuntimeReinitRequested) {
            const OutputConnectionTestResult initResult =
                ApplyOutputRuntimeFromCurrentConfig();
            outputConnectionTestOk = initResult.ok;
            outputConnectionTestMessage = initResult.message;
        }


        ImGui::PopID();
    }
    CloseGroupBox();
}

void DrawMiscScreenPage() {
    auto display=V2::HeroSettings::Snapshot()->display;
    bool displayChanged=false;
    UIGroupBox(OW::Config::uiLanguage==1?"ESP / HUD 字体":"ESP / HUD fonts");
    auto fontInput=[&](const char* label,std::string& path) {
        char text[1024]{};std::snprintf(text,sizeof(text),"%s",path.c_str());
        SettingRow(label);ImGui::SetNextItemWidth(-1);
        if(ImGui::InputText("##fontPath",text,sizeof(text),ImGuiInputTextFlags_EnterReturnsTrue)){path=text;displayChanged=true;}
    };
    ImGui::PushID("latin");fontInput(OW::Config::uiLanguage==1?"西文字体":"Font",display.font);ImGui::PopID();
    ImGui::PushID("chinese");fontInput(OW::Config::uiLanguage==1?"中文回退字体":"Chinese fallback",display.chineseFont);ImGui::PopID();
    if(ImGui::Button(OW::Config::uiLanguage==1?"恢复默认字体":"Restore default fonts")) {
        display.font="C:/Windows/Fonts/consola.ttf";display.chineseFont="C:/Windows/Fonts/msyh.ttc";displayChanged=true;
    }
    ImGui::SetItemTooltip("%s",OW::Config::uiLanguage==1?"输入字体文件完整路径，按 Enter 应用；不影响菜单字体":"Enter a font file path and press Enter; menu fonts are separate");
    for(const auto& path:{display.font,display.chineseFont})if(GetFileAttributesW(std::filesystem::u8path(path).c_str())==INVALID_FILE_ATTRIBUTES)
        ImGui::TextColored(ImVec4(1,.65f,.5f,1),"%s: %s",OW::Config::uiLanguage==1?"字体不可用，将回退":"Font unavailable; using fallback",path.c_str());
    if(!UI::OverlayFonts::error.empty())ImGui::TextWrapped("%s",UI::OverlayFonts::error.c_str());
    CloseGroupBox();
    UIGroupBox(OW::Config::uiLanguage==1?"主机状态暂停":"Host state pause");
    const auto host=V2::HostInteraction::published.load(std::memory_order_acquire);
    const bool fresh=V2::HostInteraction::Fresh(*host,OW::ProcessConnection::ConnectionEpoch(),GetTickCount());
    ImGui::BeginDisabled(!(fresh&&host->chatKnown)&&!display.pauseOnChat);
    SettingRow(OW::Config::uiLanguage==1?"聊天开启时暂停":"Pause while chatting");displayChanged|=UICheckbox("##chatPause",&display.pauseOnChat);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!(fresh&&host->focusKnown)&&!display.pauseOnFocusLoss);
    SettingRow(OW::Config::uiLanguage==1?"游戏失焦时暂停":"Pause when game loses focus");displayChanged|=UICheckbox("##focusPause",&display.pauseOnFocusLoss);
    ImGui::EndDisabled();
    ImGui::TextDisabled("%s",OW::Config::uiLanguage==1?"主机状态来源待接入":"Host state source pending");
    CloseGroupBox();
    if(displayChanged)V2::HeroSettings::SetDisplay(std::move(display));

    UIGroupBox(T(UiText::GroupTargetScreen));
    {
        bool screenChanged = false;
        int localWidth = GetSystemMetrics(SM_CXSCREEN);
        int localHeight = GetSystemMetrics(SM_CYSCREEN);
        RECT canvasRect = {};
        if (HWND canvasWindow = g_Overlay.GetOverlayWindow();
            canvasWindow && GetWindowRect(canvasWindow, &canvasRect)) {
            const int canvasWidth = canvasRect.right - canvasRect.left;
            const int canvasHeight = canvasRect.bottom - canvasRect.top;
            if (canvasWidth > 0 && canvasHeight > 0) {
                localWidth = canvasWidth;
                localHeight = canvasHeight;
            }
        }
        int detectedWidth = OW::detectedScreenWidth;
        int detectedHeight = OW::detectedScreenHeight;

        SettingRow(T(UiText::LocalWidth));
        PushControlWidth();
        ImGui::InputInt("##localScreenWidth", &localWidth, 0, 0,
                        ImGuiInputTextFlags_ReadOnly);
        ImGui::PopItemWidth();

        SettingRow(T(UiText::LocalHeight));
        PushControlWidth();
        ImGui::InputInt("##localScreenHeight", &localHeight, 0, 0,
                        ImGuiInputTextFlags_ReadOnly);
        ImGui::PopItemWidth();

        SettingRow(T(UiText::DmaWidth));
        PushControlWidth();
        ImGui::InputInt("##detectedScreenWidth", &detectedWidth, 0, 0,
                        ImGuiInputTextFlags_ReadOnly);
        ImGui::PopItemWidth();

        SettingRow(T(UiText::DmaHeight));
        PushControlWidth();
        ImGui::InputInt("##detectedScreenHeight", &detectedHeight, 0, 0,
                        ImGuiInputTextFlags_ReadOnly);
        ImGui::PopItemWidth();

        SettingRow(T(UiText::FallbackWidth));
        PushControlWidth();
        screenChanged |= ImGui::InputInt("##manualScreenWidth", &OW::Config::manualScreenWidth, 0, 0);
        if (OW::Config::manualScreenWidth < 0)
            OW::Config::manualScreenWidth = 0;
        ImGui::PopItemWidth();

        SettingRow(T(UiText::FallbackHeight));
        PushControlWidth();
        screenChanged |= ImGui::InputInt("##manualScreenHeight", &OW::Config::manualScreenHeight, 0, 0);
        if (OW::Config::manualScreenHeight < 0)
            OW::Config::manualScreenHeight = 0;
        ImGui::PopItemWidth();

        if (screenChanged)
            OW::RefreshScreenSizeFromConfig();
    }
    CloseGroupBox();
}

void DrawHeroKeyPage() {
    using namespace V2::HeroSettings;
    const auto hero=CurrentHeroOption();
    const bool zh=OW::Config::uiLanguage==1;
    auto settings=Get(hero.heroId);
    bool changed=false;
    UIGroupBox(zh?"英雄游戏按键":"Hero action keys");
    ImGui::BeginDisabled(hero.heroId==0);
    static uint64_t captureHero=0;
    static int capture=-1;
    static bool armed=false;
    if(captureHero!=hero.heroId){capture=-1;captureHero=hero.heroId;}
    for(int i=0;i<int(Action::Count);++i) {
        ImGui::PushID(i);SettingRow(kActionNames[i]);
        char label[48];const int vk=settings.keys[i];
        if(capture==i)std::snprintf(label,sizeof(label),"%s",zh?"按键… Esc 取消":"Press a key... Esc cancels");
        else if(vk==1||vk==2||vk==4||vk==5||vk==6)std::snprintf(label,sizeof(label),"M%d",vk==1?1:vk==2?2:vk==4?3:vk==5?4:5);
        else {
            wchar_t name[64]{};const UINT scan=MapVirtualKeyW(vk,MAPVK_VK_TO_VSC);
            GetKeyNameTextW(scan<<16,name,64);
            if(!WideCharToMultiByte(CP_UTF8,0,name,-1,label,sizeof(label),nullptr,nullptr)||!label[0])std::snprintf(label,sizeof(label),"VK %d",vk);
        }
        if(ImGui::Button(label,ImVec2(220,ControlHeight()))){capture=i;captureHero=hero.heroId;armed=false;}
        ImGui::PopID();
    }
    ImGui::TextDisabled("%s",zh?"鼠标侧键输出、键盘物理按住释放尚不支持；蓄力释放请用鼠标动作键。":"Side-button output and physical keyboard release are unavailable; use mouse action keys for charged releases.");
    if(capture>=0) {
        if(!armed){if(!(GetAsyncKeyState(VK_LBUTTON)&0x8000))armed=true;}
        else if(GetAsyncKeyState(VK_ESCAPE)&0x8000)capture=-1;
        else for(int vk=1;vk<256;++vk)if(SupportedKey(vk)&&(GetAsyncKeyState(vk)&0x8000)) {
            if(vk==VK_XBUTTON1||vk==VK_XBUTTON2)continue; // Current output backends emit three mouse buttons.
            settings.keys[capture]=vk;changed=true;capture=-1;break;
        }
    }
    if(ImGui::Button(zh?"恢复当前英雄默认键":"Reset this hero to default keys")){settings.keys=kDefaultKeys;changed=true;capture=-1;}
    ImGui::EndDisabled();
    if(hero.heroId==0)ImGui::TextDisabled("%s",zh?"选择英雄以编辑按键":"Select a hero to edit action keys");
    CloseGroupBox();
    if(changed) {
        std::lock_guard transition(OW::RuntimeOutputProducerTransitionMutex());
        const bool wasEnabled=OW::RuntimeOutputSessionEnabled();
        OW::SetRuntimeOutputSessionEnabled(false);
        OW::InvalidateRuntimeOutputContinuity();
        OW::CancelActiveSkill();
        OW::RuntimeOutputScheduler().CancelAllAndJoin();
        Set(hero.heroId,std::move(settings));
        OW::SetRuntimeOutputSessionEnabled(wasEnabled);
    }
    UIGroupBox(zh?"全局快捷键":"Global shortcuts");
    SettingRow(T(UiText::ToggleKey));PushControlWidth();
    int toggle=FindMenuToggleKeyIndex(OW::Config::MenuToggleKey);
    if(UISelect("##globalMenuKey",&toggle,kMenuToggleKeys,IM_ARRAYSIZE(kMenuToggleKeys)))OW::Config::MenuToggleKey=kMenuToggleVk[toggle];
    ImGui::PopItemWidth();
    SettingRow(L("Cache refresh key"));PushControlWidth();
    int refresh=OW::Config::CacheRefreshKey?FindMenuToggleKeyIndex(OW::Config::CacheRefreshKey)+1:0;
    const char* items[IM_ARRAYSIZE(kMenuToggleKeys)+1]{};items[0]=L("None");std::copy(std::begin(kMenuToggleKeys),std::end(kMenuToggleKeys),items+1);
    if(UISelect("##globalRefreshKey",&refresh,items,IM_ARRAYSIZE(items)))OW::Config::CacheRefreshKey=refresh?kMenuToggleVk[refresh-1]:0;
    if(OW::Config::CacheRefreshKey==OW::Config::MenuToggleKey)OW::Config::CacheRefreshKey=0;
    ImGui::PopItemWidth();CloseGroupBox();
}
