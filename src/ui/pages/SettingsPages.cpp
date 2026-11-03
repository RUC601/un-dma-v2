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
#include "Game/profiles/CandidateSession.hpp"
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

        ImGui::TextWrapped("%s", T(UiText::SavePolicyNotice));

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
            OW::Config::uiLanguage = OW::Config::ClampUiLanguage(uiLanguage);
        ImGui::PopItemWidth();

        SettingRow(T(UiText::ToggleKey));
        PushControlWidth();
        int toggleKeyIndex = FindMenuToggleKeyIndex(OW::Config::MenuToggleKey);
        OW::Config::MenuToggleKey = kMenuToggleVk[toggleKeyIndex];
        if (UISelect("##menuToggleKey", &toggleKeyIndex, kMenuToggleKeys, IM_ARRAYSIZE(kMenuToggleKeys)))
            OW::Config::MenuToggleKey = kMenuToggleVk[toggleKeyIndex];
        ImGui::PopItemWidth();
    }
    CloseGroupBox();

    UIGroupBox(T(UiText::GroupApplication));
    {
        SettingRow(T(UiText::Unleashed));
        if (UIActionButton(T(UiText::CloseUn), 96.0f))
            g_Overlay.RequestExit();
        ImGui::SameLine();
        const bool reconnectBusy =
            OW::ProcessConnection::IsConnecting() ||
            OW::ProcessConnection::IsReconnectRequested() ||
            OW::ProcessConnection::IsDmaRecoveryRequested();
        if (reconnectBusy)
            ImGui::BeginDisabled();
        if (UIActionButton(T(UiText::ReconnectUn), 112.0f))
            OW::ProcessConnection::RequestReconnect();
        if (reconnectBusy)
            ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", T(UiText::TooltipReconnect));

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

        SettingRow(T(UiText::RuntimeVersion));
        const auto layout = V2::Profiles::Snapshot();
        if (OW::ProcessConnection::IsConnected() && layout.detection.selected >= 0)
            ImGui::TextWrapped("%s%s", V2::Profiles::profiles[layout.detection.selected].id,
                layout.candidateSession ? " / candidate" : "");
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
    const auto profile=V2::Profiles::Snapshot();
    UIGroupBox("版本与布局");
    int selected=V2::Profiles::requestedProfile.load()+1;
    const char* choices[]={"自动 / Auto (CN)","CN", "KR 候选 / Candidate", "BZ 候选 / Candidate"};
    ImGui::SetNextItemWidth(-1);
    if(ImGui::Combo("##RequestedProfile",&selected,choices,IM_ARRAYSIZE(choices))) {
        V2::Profiles::requestedProfile.store(selected-1);
        OW::ProcessConnection::RequestReconnect();
    }
    ImGui::TextWrapped("选择用于本次运行，切换后重新核验。KR/BZ 仅运行候选读取，UN 业务与输出关闭。");
    if(profile.candidateSession)
        ImGui::TextWrapped("Candidate: status=%d slots=%u/%u parents=%u",profile.candidateStatus,
            profile.candidateScanned,profile.candidateCapacity,profile.candidateCount);

    ImGui::TextWrapped("%s", V2::Profiles::StatusText(profile.detection.status));
    if(profile.detection.selected>=0)
        ImGui::TextWrapped("%s", V2::Profiles::profiles[profile.detection.selected].id);
    ImGui::TextWrapped("CN 映像：%s；相机兼容与实体可用分别检查。", profile.cnImage?"已核验":"未核验");
    ImGui::TextWrapped("KR / BZ：相机、注册表分块扫描及解码候选已接入；实服未验证。实体字段到 UN 业务适配尚未完成。");
    for (const auto& item : V2::Profiles::Catalog()) {
        ImGui::TextWrapped("%s · camera=%d registry=%d decode=%d UN=%d output=%d %s",
            item.id,item.camera,item.registry,item.componentDecode,item.unFields,item.output,
            item.candidate ? "candidate / live unverified" : "CN implementation");
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
    UIGroupBox(T(UiText::GroupBehavior));
    {
        static int selectedBehavior = 0;
        static int selectedBehaviorPresetId = -1;
        static int lastBehaviorPresetId = -2;
        static char behaviorPresetName[64] = "Behavior Preset";
        selectedBehavior = OW::Config::ClampAimBehaviorIndex(selectedBehavior);

        SettingRow(T(UiText::SubBehavior));
        PushControlWidth();
        if (UISelect("##miscBehaviorSelector", &selectedBehavior,
                     kAimBehavior, IM_ARRAYSIZE(kAimBehavior))) {
            if (const OW::Config::AimBehaviorPreset* selectedPreset =
                    OW::Config::FindAimBehaviorPreset(selectedBehaviorPresetId)) {
                if (OW::Config::ClampAimBehaviorIndex(selectedPreset->behavior) != selectedBehavior)
                    selectedBehaviorPresetId = -1;
            }
        }
        ImGui::PopItemWidth();

        SettingRow(T(UiText::Preset));
        PushControlWidth();
        if (UISelectAimBehaviorPreset("##miscBehaviorPreset", &selectedBehaviorPresetId, selectedBehavior)) {
            if (const OW::Config::AimBehaviorPreset* selectedPreset =
                    OW::Config::FindAimBehaviorPreset(selectedBehaviorPresetId)) {
                selectedBehavior = OW::Config::ClampAimBehaviorIndex(selectedPreset->behavior);
                CopyPresetNameToBuffer(selectedPreset->name, behaviorPresetName);
            }
        }
        ImGui::PopItemWidth();

        selectedBehavior = OW::Config::ClampAimBehaviorIndex(selectedBehavior);
        const int behaviorIndex = selectedBehavior;
        OW::Config::AimBehaviorPreset* selectedPreset =
            OW::Config::FindMutableAimBehaviorPreset(selectedBehaviorPresetId);
        if (selectedPreset &&
            OW::Config::ClampAimBehaviorIndex(selectedPreset->behavior) != behaviorIndex) {
            selectedBehaviorPresetId = -1;
            selectedPreset = nullptr;
        }
        if (lastBehaviorPresetId != selectedBehaviorPresetId) {
            if (selectedPreset)
                CopyPresetNameToBuffer(selectedPreset->name, behaviorPresetName);
            else
                CopyPresetNameToBuffer("Behavior Preset", behaviorPresetName);
            lastBehaviorPresetId = selectedBehaviorPresetId;
        }

        SettingRow(T(UiText::PresetName));
        PushControlWidth();
        if (ImGui::InputText("##miscBehaviorPresetName", behaviorPresetName, IM_ARRAYSIZE(behaviorPresetName))) {
            if (selectedPreset)
                selectedPreset->name = NormalizeUiPresetName(behaviorPresetName, "Behavior Preset", -1);
        }
        ImGui::PopItemWidth();

        int& behaviorMethod = selectedPreset
            ? selectedPreset->method
            : OW::Config::View_aimBehaviorMethod()[static_cast<size_t>(behaviorIndex)];
        int& behaviorMethodPresetId = selectedPreset
            ? selectedPreset->methodPresetId
            : OW::Config::View_aimBehaviorMethodPreset()[static_cast<size_t>(behaviorIndex)];
        float& behaviorBaseSpeed = selectedPreset
            ? selectedPreset->baseSpeed
            : OW::Config::aimBehaviorBaseSpeed[static_cast<size_t>(behaviorIndex)];
        OW::AimStartLimiterProfile& behaviorStartLimiter = selectedPreset
            ? selectedPreset->startLimiter
            : OW::Config::aimBehaviorStartLimiterProfiles[static_cast<size_t>(behaviorIndex)];
        bool& behaviorMoveSplitEnabled = selectedPreset
            ? selectedPreset->moveSplitEnabled
            : OW::Config::aimBehaviorMoveSplitEnabled[static_cast<size_t>(behaviorIndex)];
        int& behaviorMoveSplitMaxPixels = selectedPreset
            ? selectedPreset->moveSplitMaxPixels
            : OW::Config::aimBehaviorMoveSplitMaxPixels[static_cast<size_t>(behaviorIndex)];
        int& behaviorMoveSplitDelayUs = selectedPreset
            ? selectedPreset->moveSplitDelayUs
            : OW::Config::aimBehaviorMoveSplitDelayUs[static_cast<size_t>(behaviorIndex)];
        behaviorMethod = OW::Config::ClampAimMethodIndex(behaviorMethod);
        behaviorMethodPresetId = OW::Config::ClampAimMethodPresetId(behaviorMethodPresetId);
        if (const OW::Config::AimMethodPreset* methodPreset =
                OW::Config::FindAimMethodPreset(behaviorMethodPresetId)) {
            behaviorMethod = OW::Config::ClampAimMethodIndex(methodPreset->method);
        }
        behaviorMoveSplitMaxPixels = OW::Config::ClampMoveSplitMaxPixels(behaviorMoveSplitMaxPixels);
        behaviorMoveSplitDelayUs = OW::Config::ClampMoveSplitDelayUs(behaviorMoveSplitDelayUs);
        behaviorStartLimiter = OW::ValidateAimStartLimiterProfile(behaviorStartLimiter);
        OW::Config::View_aimMethod() = behaviorMethod;

        SettingRow(T(UiText::SubMethod));
        PushControlWidth();
        if (UISelect("##miscBehaviorMethod", &behaviorMethod,
                     kAimMethod, IM_ARRAYSIZE(kAimMethod))) {
            behaviorMethod = OW::Config::ClampAimMethodIndex(behaviorMethod);
            if (const OW::Config::AimMethodPreset* methodPreset =
                    OW::Config::FindAimMethodPreset(behaviorMethodPresetId)) {
                if (OW::Config::ClampAimMethodIndex(methodPreset->method) != behaviorMethod)
                    behaviorMethodPresetId = -1;
            }
            OW::Config::View_aimMethod() = behaviorMethod;
        }
        ImGui::PopItemWidth();

        SettingRow(T(UiText::MethodPreset));
        PushControlWidth();
        if (UISelectAimMethodPreset("##miscBehaviorMethodPreset", &behaviorMethodPresetId, behaviorMethod)) {
            if (const OW::Config::AimMethodPreset* methodPreset =
                    OW::Config::FindAimMethodPreset(behaviorMethodPresetId)) {
                behaviorMethod = OW::Config::ClampAimMethodIndex(methodPreset->method);
                OW::Config::View_aimMethod() = behaviorMethod;
            }
        }
        ImGui::PopItemWidth();

        SettingRow(T(UiText::BaseAngularSpeed));
        PushControlWidth();
        UISlider("##miscBaseAngularSpeed", &behaviorBaseSpeed, 0.0f, 100.0f, Number(0, " %"));
        ImGui::PopItemWidth();

        if (OW::Config::IsTrackingBehavior(behaviorIndex)) {
            SettingRow(T(UiText::StartSpeedLimit));
            UICheckbox("##miscBehaviorStartSpeedLimit", &behaviorStartLimiter.enabled);

            if (behaviorStartLimiter.enabled) {
                SettingRow(T(UiText::InitialSpeedCap));
                PushControlWidth();
                UISlider(
                    "##miscBehaviorStartInitialCap",
                    &behaviorStartLimiter.initialCapDegPerSec,
                    0.0f,
                    OW::kAimStartLimiterMaxCapDegPerSec, Number(0, " deg/s"));
                ImGui::PopItemWidth();

                SettingRow(T(UiText::CapRise));
                PushControlWidth();
                UISlider(
                    "##miscBehaviorStartCapRise",
                    &behaviorStartLimiter.capRiseDegPerSec2,
                    0.0f,
                    10000.0f, Number(0, " deg/s^2"));
                ImGui::PopItemWidth();

                SettingRow(T(UiText::RestartOnTargetChange));
                UICheckbox(
                    "##miscBehaviorStartRestartOnTargetChange",
                    &behaviorStartLimiter.restartOnTargetChange);
            }
        }

        if (behaviorIndex == OW::Config::kAimBehaviorFlick) {
            SettingRow("连续 Flick 移动窗口（全局）");
            PushControlWidth();
            UISlider("##miscFlickRepeatWindow", &OW::Config::flickRepeatWindowMs,
                1.0f, 1000.0f, Number(0, " ms"));
            ImGui::PopItemWidth();
            SettingRow("连续 Flick 速度上限（全局）");
            PushControlWidth();
            UISlider("##miscFlickRepeatMaxSpeed", &OW::Config::flickRepeatMaxSpeedDegPerSec,
                1.0f, 3600.0f, Number(0, " deg/s"));
            ImGui::PopItemWidth();
            ImGui::TextDisabled("普通 Flick 命中后开枪；延时和故意打偏设置不强制开枪。");
        }

        SettingRow(T(UiText::MoveSplit));
        UICheckbox("##miscBehaviorMoveSplit", &behaviorMoveSplitEnabled);

        if (behaviorMoveSplitEnabled) {
            SettingRow(T(UiText::SplitChunk));
            PushControlWidth();
            UISlider("##miscBehaviorMoveSplitMax", &behaviorMoveSplitMaxPixels, 1.0f, 50.0f, Number(0, " px"));
            ImGui::PopItemWidth();

            SettingRow(T(UiText::SplitDelay));
            PushControlWidth();
            UISlider("##miscBehaviorMoveSplitDelay", &behaviorMoveSplitDelayUs, 0.0f, 10000.0f, Number(0, " us"));
            ImGui::PopItemWidth();
        }

        if (selectedPreset) {
            selectedPreset->behavior = behaviorIndex;
            selectedPreset->name = NormalizeUiPresetName(behaviorPresetName, "Behavior Preset", -1);
        }

        ImGui::Spacing();
        if (UIActionButton(T(UiText::SaveNew), 92.0f)) {
            OW::Config::AimBehaviorPreset preset = CaptureBehaviorPresetFromCurrent(
                behaviorIndex,
                behaviorMethod,
                behaviorMethodPresetId,
                behaviorBaseSpeed,
                behaviorStartLimiter,
                behaviorMoveSplitEnabled,
                behaviorMoveSplitMaxPixels,
                behaviorMoveSplitDelayUs,
                behaviorPresetName);
            OW::Config::View_aimBehaviorPresets().push_back(preset);
            selectedBehaviorPresetId = preset.id;
            lastBehaviorPresetId = -2;
            selectedPreset = nullptr;
        }
        if (selectedPreset) {
            ImGui::SameLine(0.0f, 8.0f);
            if (UIActionButton(T(UiText::Delete), 72.0f)) {
                const int deletedId = selectedBehaviorPresetId;
                OW::Config::View_aimBehaviorPresets().erase(
                    std::remove_if(OW::Config::View_aimBehaviorPresets().begin(),
                                   OW::Config::View_aimBehaviorPresets().end(),
                                   [deletedId](const OW::Config::AimBehaviorPreset& preset) {
                                       return preset.id == deletedId;
                                   }),
                    OW::Config::View_aimBehaviorPresets().end());
                if (OW::Config::View_aimBehaviorPresetId() == deletedId)
                    OW::Config::View_aimBehaviorPresetId() = -1;
                selectedBehaviorPresetId = -1;
                lastBehaviorPresetId = -2;
            }
        }

        SettingRow(T(UiText::PitchScale));
        PushControlWidth();
        UISlider("##miscPitchScale", &OW::Config::View_aimbotPitchScale(), 0.1f, 3.0f, Number(2, ""));
        ImGui::PopItemWidth();

        SettingRow(T(UiText::OvershootCurve));
        UICheckbox("##miscOvershootCurve", &OW::Config::aimOvershootCurve);

        if (OW::Config::aimOvershootCurve) {
            SettingRow(T(UiText::OvershootGain));
            PushControlWidth();
            UISlider("##miscOvershootGain", &OW::Config::aimOvershootGain, 0.0f, 1.0f, Number(2, ""));
            ImGui::PopItemWidth();

            SettingRow(T(UiText::OvershootReset));
            PushControlWidth();
            UISlider("##miscOvershootReset", &OW::Config::aimOvershootResetPixels, 1.0f, 250.0f, Number(0, " px"));
            ImGui::PopItemWidth();
        }
    }
    CloseGroupBox();
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

        editPreset.pointCount = std::clamp(editPreset.pointCount, 2, OW::Config::kMaxDynamicFovPoints);
        SettingRow("Point Count");
        PushControlWidth();
        UISlider("##miscDynamicFovPointCount",
                 &editPreset.pointCount,
                 2.0f,
                 static_cast<float>(OW::Config::kMaxDynamicFovPoints), Number(0, ""));
        editPreset.pointCount = std::clamp(editPreset.pointCount, 2, OW::Config::kMaxDynamicFovPoints);
        ImGui::PopItemWidth();

        for (int index = 0; index < editPreset.pointCount; ++index) {
            char distanceLabel[32] = {};
            char distanceId[48] = {};
            char fovLabel[32] = {};
            char fovId[48] = {};
            std::snprintf(distanceLabel, sizeof(distanceLabel), L("Point %d Dist"), index + 1);
            std::snprintf(distanceId, sizeof(distanceId), "##dynamicFovPoint%dDistance", index);
            std::snprintf(fovLabel, sizeof(fovLabel), L("Point %d FOV"), index + 1);
            std::snprintf(fovId, sizeof(fovId), "##dynamicFovPoint%dFov", index);

            SettingRow(distanceLabel);
            PushControlWidth();
            UISlider(distanceId,
                     &editPreset.points[static_cast<size_t>(index)].distanceM,
                     OW::Config::kMinDynamicFovDistanceM,
                     100.0f, Number(0, " m"));
            ImGui::PopItemWidth();

            SettingRow(fovLabel);
            PushControlWidth();
            UISlider(fovId,
                     &editPreset.points[static_cast<size_t>(index)].fovDeg,
                     OW::Config::kMinFovDeg,
                     OW::Config::kMaxFovDeg, Number(0, " deg"));
            ImGui::PopItemWidth();
        }

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
            ImGui::SetTooltip("%s", L("Reads current in-game sensitivity from the live singleton path when available, with input.MouseScaleX/Y as the BZ fallback. Manual sensitivity remains the final fallback."));
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
