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
#include "ui/vision/ContourPanel.hpp"
using UI::Text::UiText;
using UI::Text::kUiLanguageOptions;
#include "game/profiles/ProfileState.hpp"
#include "Features/UiLabels.hpp"
#include "Features/UiValueFormat.hpp"
using namespace UI::ValueFormat;
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
void DrawMiscCapturePage();

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
        case 7:
            DrawMiscCapturePage();
            break;
        default:
            DrawMiscGeneralPage();
            break;
    }

    CloseGroupBox();

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

        SettingTooltip(T(UiText::SavePolicyNotice));

        const float profileActionButtonWidth = (std::max)(
            68.0f,
            (std::max)(ImGui::CalcTextSize(T(UiText::Open)).x, ImGui::CalcTextSize(T(UiText::Create)).x) + 24.0f);
        SettingRow(T(UiText::Folder));
        {
            char folderBuffer[MAX_PATH] = {};
            const std::string configDirectory = OW::Config::ConfigDirectoryPath();
            std::snprintf(folderBuffer, sizeof(folderBuffer), "%s", configDirectory.c_str());
            const float spacing = ImGui::GetStyle().ItemSpacing.x + kControlRightPadding;
            const float folderWidth = MaxFloat(
                120.0f,
                ImGui::GetContentRegionAvail().x - profileActionButtonWidth - spacing);
            ImGui::PushItemWidth(folderWidth);
            ImGui::InputText("##configFolder", folderBuffer, IM_ARRAYSIZE(folderBuffer),
                             ImGuiInputTextFlags_ReadOnly);
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (ImGui::Button(T(UiText::Open), ImVec2(profileActionButtonWidth, ControlHeight())))
                ShellExecuteA(nullptr, "open", configDirectory.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }

        SettingRow(T(UiText::NewProfile));
        {
            const float spacing = ImGui::GetStyle().ItemSpacing.x + kControlRightPadding;
            const float inputWidth = MaxFloat(
                90.0f,
                ImGui::GetContentRegionAvail().x - profileActionButtonWidth - spacing);
            ImGui::PushItemWidth(inputWidth);
            ImGui::InputText("##newConfigProfile", newProfileName, IM_ARRAYSIZE(newProfileName));
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (ImGui::Button(T(UiText::Create), ImVec2(profileActionButtonWidth, ControlHeight()))) {
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
        const float appGap = ImGui::GetStyle().ItemSpacing.x;
        const float appHalfWidth = 108.0f;
        const float appFullWidth = appHalfWidth * 2.0f + appGap;
        SettingRow(T(UiText::Unleashed));
        if (ImGui::Button(T(UiText::CloseUn), ImVec2(appHalfWidth, ControlHeight())))
            g_Overlay.RequestExit();
        ImGui::SameLine();
        const bool reconnectBusy = OW::ProcessConnection::IsRecoveryBusy();
        if (reconnectBusy)
            ImGui::BeginDisabled();
        if (ImGui::Button(T(UiText::ReconnectUn), ImVec2(appHalfWidth, ControlHeight())))
            OW::ProcessConnection::RequestReconnect();
        if (reconnectBusy)
            ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", T(UiText::TooltipReconnect));

        SettingRow(L("Runtime cache"));
        if (reconnectBusy)
            ImGui::BeginDisabled();
        if (ImGui::Button(L("Refresh caches"), ImVec2(appFullWidth, ControlHeight()))) {
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
        if (ImGui::Button(L("Rebuild DMA connection"), ImVec2(appFullWidth, ControlHeight()))) {
            StopFirePatternRecorder();
            OW::ProcessConnection::RequestDmaRecovery();
        }
        if (reconnectBusy)
            ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", L("Stops the current session, reloads DMA device settings, and rebuilds the device connection. Old data and commands are discarded."));

        SettingRow(T(UiText::Overlay));
        if (ImGui::Button(T(UiText::MinimizeOverlay), ImVec2(appFullWidth, ControlHeight())))
            g_Overlay.MinimizeToTaskbar();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", T(UiText::TooltipMinimizeOverlay));

        SettingRow(L("Overlay background mode"));
        PushControlWidth();
        const char* bgModes[] = { "Opaque black", "Transparent background" };
        int bgMode = OW::Config::overlayTransparentBackground ? 1 : 0;
        if (UISelect("##overlayBackgroundMode", &bgMode, bgModes, 2)) {
            const bool transparentBackground = (bgMode == 1);
            if (!g_Overlay.SetCanvasTransparentBackground(transparentBackground))
                OW::Config::overlayTransparentBackground = g_Overlay.IsCanvasTransparentBackground();
            else
                OW::Config::overlayTransparentBackground = transparentBackground;
        }
        ImGui::PopItemWidth();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", L("Opaque black keeps a solid black canvas for fusers and maximum FPS. Transparent background uses DirectComposition so the desktop and underlying windows show through empty canvas pixels while HUD and boxes draw normally."));

        SettingRow(L("Overlay frame limit"));
        PushControlWidth();
        UISlider("##overlayFrameLimit", &OW::Config::overlayFrameLimit, 0, 2000,
                 AtOrBelow(L("Unlimited"), 0, Number(0, " FPS")));
        ImGui::PopItemWidth();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", L("0 = unlimited. Changes apply immediately. Only limits canvas drawing; data acquisition stays at 300 Hz and the menu at about 60 FPS."));

        SettingRow(L("Actual overlay FPS"));
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%.0f FPS", Diagnostics::RenderFps());

        SettingRow(T(UiText::RuntimeVersion));
        ImGui::AlignTextToFramePadding();
        const auto layout = V2::Profiles::Snapshot();
        const auto* matched = V2::Profiles::MatchedProfile(layout.detection);
        if (OW::ProcessConnection::IsConnected() && matched)
            ImGui::TextWrapped("%s %s", matched->region, matched->build);
        else {
            ImGui::TextDisabled("%s", OW::Config::uiLanguage == 1 ? "未识别" : "Unrecognized");
            SettingTooltip(L("No verified active layout. Open Diagnostics to choose or retry."));
        }

        SettingRow(T(UiText::Process));
        ImGui::AlignTextToFramePadding();
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
            ImGui::SameLine(0, 8);
            ImGui::TextDisabled("PID %d", OW::ProcessConnection::ConnectedPid());
        }
    }
    CloseGroupBox();
}

void DrawMiscDiagnosticsPage() {
    const auto profile = V2::Profiles::Snapshot();
    const auto* matched = V2::Profiles::MatchedProfile(profile.detection);
    const bool zh = OW::Config::uiLanguage == 1;
    UIGroupBox(zh ? "版本与布局" : "Version and layout");
    int selected = V2::Profiles::requestedProfile.load() + 1;
    const char* choicesEn[] = {"Auto", "CN", "World", "Nexon"};
    const char* choicesZh[] = {"自动识别 (Auto)", "国服 (CN)", "国际服 (World)", "韩服 (Nexon)"};
    SettingRow(zh ? "目标区服" : "Target region");
    PushControlWidth();
    if (UISelect("##RequestedProfile", &selected, zh ? choicesZh : choicesEn, 4, false)) {
        V2::Profiles::requestedProfile.store(selected - 1);
        OW::ProcessConnection::RequestReconnect();
    }
    ImGui::PopItemWidth();
    SettingRow(zh ? "匹配状态" : "Layout status");
    ImGui::AlignTextToFramePadding();
    if (matched)
        ImGui::TextWrapped(zh ? "已识别 %s · %s" : "Matched %s · %s", matched->region, matched->build);
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
        SettingRow(T(UiText::DryRunLogOnly));
        UICheckbox("##DryRunLogOnly", &OW::Config::aimDryRun);
        if (OW::Config::aimDryRun) {
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "%s", T(UiText::NoCursorMovement));
        }
        bool showLogOverlay = Diagnostics::IsLogOverlayVisible();
        SettingRow(T(UiText::ShowLogOverlay));
        if (UICheckbox("##ShowLogOverlay", &showLogOverlay))
            Diagnostics::SetLogOverlayVisible(showLogOverlay);
        ImGui::SameLine(0, 10);
        if (ImGui::Button(T(UiText::ClearLog), ImVec2(0, ControlHeight())))
            Diagnostics::ClearLogLines();
        SettingRow(T(UiText::VerboseAimLogs));
        UICheckbox("##VerboseAimLogs", &OW::Config::aimVerboseLog);
        SettingTooltip(T(UiText::VerboseLogsWarning));
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
    if(UISelect("##modeBehavior",&mode.behavior,kAimBehavior,IM_ARRAYSIZE(kAimBehavior))) {
        if(!selected) {
            mode.firePolicy=static_cast<int>(OW::DefaultFirePolicyForBehavior(
                static_cast<OW::AimBehaviorType>(mode.behavior)));
            mode.keepFiring=mode.firePolicy==static_cast<int>(OW::FirePolicyType::KeepFiring) ||
                mode.firePolicy==static_cast<int>(OW::FirePolicyType::TrackingWithHold);
        }
        changed=true;
    }
    ImGui::PopItemWidth();
    ImGui::EndDisabled();
    SettingTooltip(L("Edit mode structure and defaults here. Existing Slot parameters stay independent."));
    changed|=UI::BehaviorTuningEditor(mode,mode.behavior);
    changed|=UI::AimPlanEditor::Motion(mode.motion);
    mode.method=mode.motion.method;mode.methodPresetId=-1;
    const bool modeListFull = std::count_if(Read_aimBehaviorPresets().begin(), Read_aimBehaviorPresets().end(),
        [](const auto& p) { return p.defaultBehavior < 0; }) >= 64;
    SettingRow(OW::Config::uiLanguage == 1 ? "模式操作" : "Mode actions");
    ImGui::BeginDisabled(modeListFull);
    if(UIActionButton(L("Save as new mode"),124.f)) {
        auto created=CaptureBehaviorPresetFromCurrent(mode.behavior,mode.method,-1,mode.baseSpeed,
            mode.startLimiter,mode.moveSplitEnabled,mode.moveSplitMaxPixels,mode.moveSplitDelayUs,name);
        static_cast<AimBehaviorTuning&>(created)=mode;created.motion=mode.motion;
        View_aimBehaviorPresets().push_back(created);selectedId=created.id;lastId=-2;changed=true;
    }
    ImGui::EndDisabled();
    if(modeListFull) SettingTooltip(L("The mode list is full (64). Delete an unused mode to add another."));
    if(selectedId>=0) {
        const auto* deleteCandidate=FindAimBehaviorPreset(selectedId);
        const bool protectedDefault=deleteCandidate && deleteCandidate->defaultBehavior>=0;
        const bool used=AimBehaviorPresetInUse(selectedId);
        ImGui::SameLine();ImGui::BeginDisabled(used);
        if(UIActionButton(L("Delete mode"),124.f)) {
            std::erase_if(View_aimBehaviorPresets(),[&](const auto& p){return p.id==selectedId && p.defaultBehavior<0;});
            selectedId=-1;lastId=-2;changed=true;
        }
        ImGui::EndDisabled();
        if(protectedDefault) SettingTooltip(L("Default modes cannot be deleted."));
        else if(used) SettingTooltip(L("This mode is in use. Choose another mode in its Slots before deleting it."));
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
                SettingTooltip(L("Save a new method preset to edit Shuwu parameters."));
            ImGui::BeginDisabled(!selectedPreset);
            UI::AimPlanEditor::Shuwu(selectedPreset ? selectedPreset->shuwu : defaults);
            ImGui::EndDisabled();
            break;
        }
        case 8: {
            drawAngularSpeed("##methodResponseAngularSpeed");
            OW::AimResponseCurve::Points defaults;
            if(!selectedPreset) SettingTooltip(L("Save a new method preset to edit response curve parameters."));
            ImGui::BeginDisabled(!selectedPreset);
            UI::ResponseCurveEditor(selectedPreset?selectedPreset->responseCurve:defaults);
            ImGui::EndDisabled();break;
        }
        case 9:
            drawAngularSpeed("##methodKoiAngularSpeed");
            SettingTooltip(L("Follows the remaining aim error proportionally. Speed scale adjusts response."));
            break;
        default:
            break;
        }

        if (selectedPreset) {
            selectedPreset->method = selectedMethod;
            selectedPreset->name = NormalizeUiPresetName(methodPresetName, "Method Preset", -1);
        }

        SettingRow(OW::Config::uiLanguage == 1 ? "预设操作" : "Preset actions");
        if (UIActionButton(T(UiText::SaveNew), 112.0f)) {
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
        static uint64_t lastProfileGeneration = 0;
        const auto generation = OW::Config::configLoadGeneration.load(std::memory_order_acquire);
        if (generation != lastProfileGeneration) {
            lastPresetId = -2;
            lastProfileGeneration = generation;
        }

        const OW::Config::DynamicFovPreset* selectedPreset =
            OW::Config::FindDynamicFovPreset(selectedPresetId);
        if (lastPresetId != selectedPresetId) {
            if (selectedPreset) {
                draft = *selectedPreset;
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
            selectedPreset = OW::Config::FindDynamicFovPreset(selectedPresetId);
            if (selectedPreset) {
                draft = *selectedPreset;
                CopyPresetNameToBuffer(selectedPreset->name, presetName);
            } else {
                draft = OW::Config::DynamicFovPreset{};
                CopyPresetNameToBuffer("Dynamic FOV", presetName);
            }
            lastPresetId = selectedPresetId;
        }
        ImGui::PopItemWidth();

        selectedPreset = OW::Config::FindDynamicFovPreset(selectedPresetId);
        OW::Config::DynamicFovPreset& editPreset = draft;

        SettingRow("Preset Name");
        PushControlWidth();
        ImGui::InputText("##miscDynamicFovName", presetName, IM_ARRAYSIZE(presetName));
        ImGui::PopItemWidth();

        SettingRow(OW::Config::uiLanguage == 1 ? "覆盖宽度（米）" : "Coverage width (m)");
        PushControlWidth();
        UISlider("##worldDiameterM", &editPreset.worldDiameterM, 0.0f, 50.0f, Number(2, " m"));
        ImGui::SetItemTooltip("%s", OW::Config::uiLanguage == 1 ? "0：编辑曲线节点；大于 0：按距离保持固定的覆盖宽度。" : "0 edits curve points; a positive value keeps this world-space width at every distance.");
        ImGui::PopItemWidth();
        SettingRow("Smooth Curve");
        UICheckbox("##miscDynamicFovSmooth", &editPreset.smooth);

        UI::DynamicFovEditor(editPreset);

        editPreset = OW::Config::ValidateDynamicFovPresetValue(editPreset);

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
        SettingRow(OW::Config::uiLanguage == 1 ? "距离采样" : "Sampled FOV");
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(0.78f, 0.84f, 0.92f, 1.0f), "%s", preview);

        SettingRow(OW::Config::uiLanguage == 1 ? "预设操作" : "Preset actions");
        if (selectedPreset && UIActionButton(OW::Config::uiLanguage == 1 ? "更新预设" : "Update Preset", 112.0f)) {
            editPreset.name = NormalizeUiPresetName(presetName, "Dynamic FOV", -1);
            editPreset.id = selectedPresetId;
            if (auto* destination = OW::Config::FindMutableDynamicFovPreset(selectedPresetId))
                *destination = OW::Config::ValidateDynamicFovPresetValue(editPreset);
        }
        if (selectedPreset) ImGui::SameLine(0.0f, 8.0f);
        if (UIActionButton(T(UiText::SaveNew), 112.0f)) {
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

void DrawMiscScreenPage() {
    auto display=V2::HeroSettings::Snapshot()->display;
    bool displayChanged=false;
    UIGroupBox(OW::Config::uiLanguage==1?"ESP / HUD 字体":"ESP / HUD fonts");
    auto fontInput=[&](const char* label,std::string& path) {
        char text[1024]{};std::snprintf(text,sizeof(text),"%s",path.c_str());
        SettingRow(label);PushControlWidth();
        if(ImGui::InputText("##fontPath",text,sizeof(text),ImGuiInputTextFlags_EnterReturnsTrue)){path=text;displayChanged=true;}
        ImGui::PopItemWidth();
    };
    ImGui::PushID("latin");fontInput(OW::Config::uiLanguage==1?"西文字体":"Font",display.font);ImGui::PopID();
    ImGui::PushID("chinese");fontInput(OW::Config::uiLanguage==1?"中文回退字体":"Chinese fallback",display.chineseFont);ImGui::PopID();
    SettingRow(OW::Config::uiLanguage==1?"默认字体":"Default fonts");
    if(ImGui::Button(OW::Config::uiLanguage==1?"恢复默认字体":"Restore default fonts", ImVec2(0, ControlHeight()))) {
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
    if (!(fresh && host->chatKnown))
        SettingTooltip(OW::Config::uiLanguage == 1 ? "主机状态不可用" : "Host state unavailable");
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!(fresh&&host->focusKnown)&&!display.pauseOnFocusLoss);
    SettingRow(OW::Config::uiLanguage==1?"游戏失焦时暂停":"Pause when game loses focus");displayChanged|=UICheckbox("##focusPause",&display.pauseOnFocusLoss);
    if (!(fresh && host->focusKnown))
        SettingTooltip(OW::Config::uiLanguage == 1 ? "主机状态不可用" : "Host state unavailable");
    ImGui::EndDisabled();

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

        const float hudAutoScale = OW::OverlayStatus::ResolutionScale(
            static_cast<float>(localWidth), static_cast<float>(localHeight));
        SettingRow(OW::Config::uiLanguage == 1 ? "HUD 基准缩放" : "HUD layout scale");
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%.2fx (%dx%d -> 1080p)", hudAutoScale, localWidth, localHeight);
        SettingTooltip(OW::Config::uiLanguage == 1
            ? "以 1920x1080 为 1.00x 基准，在 2K/4K 画布上自动保持一致的 HUD 与 ESP 排布比例。"
            : "Relative to the 1920x1080 (1.00x) baseline so 2K/4K canvases keep consistent HUD and ESP proportions.");

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

void DrawMiscCapturePage() {
    UI::Vision::DrawPage();
}

void DrawHeroKeyPage() {
    using namespace V2::HeroSettings;
    const auto hero=CurrentHeroOption();
    const bool zh=OW::Config::uiLanguage==1;
    auto settings=Get(hero.heroId);
    bool changed=false;
    const bool hasHero = hero.heroId != 0;
    const auto drawHeroKeys = [&](Action first, Action end) {
        for (int i = static_cast<int>(first); i < static_cast<int>(end); ++i) {
            ImGui::PushID(i);
            SettingRow(kActionNames[i]);
            PushControlWidth();
            changed |= UIKeyBinding("##heroActionKey", &settings.keys[i],
                                    KeyBindingStorage::VirtualKey, false);
            if (hasHero)
                SettingTooltip(zh ? "鼠标侧键输出、键盘物理按住释放尚不支持；蓄力释放请用鼠标动作键。"
                                       : "Side-button output and physical keyboard release are unavailable; use mouse action keys for charged releases.");
            else
                SettingTooltip(zh ? "选择英雄以编辑按键" : "Select a hero to edit action keys");
            ImGui::PopItemWidth();
            ImGui::PopID();
        }
    };

    ImGui::BeginDisabled(!hasHero);
    UIGroupPair(zh ? "英雄游戏按键" : "Hero action keys", [&]() {
        drawHeroKeys(Action::Primary, Action::Jump);
    }, zh ? "移动与其他按键" : "Movement and other keys", [&]() {
        drawHeroKeys(Action::Jump, Action::Count);
        SettingRow(zh ? "默认设置" : "Default keys");
        const float avail = MaxFloat(80.0f, ImGui::GetContentRegionAvail().x - kControlRightPadding);
        if (ImGui::Button(zh ? "恢复当前英雄默认键" : "Reset this hero to default keys", ImVec2(avail, ControlHeight()))) {
            settings.keys = kDefaultKeys;
            changed = true;
            UIKeyBindingCancelCapture();
        }
    });
    ImGui::EndDisabled();
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
    UIGroupPair(zh ? "全局快捷键" : "Global shortcuts", [&]() {
        SettingRow(T(UiText::ToggleKey));
        PushControlWidth();
        UIKeyBinding("##globalMenuKey", &OW::Config::MenuToggleKey, KeyBindingStorage::VirtualKey);
        ImGui::PopItemWidth();

        SettingRow(L("Cache refresh key"));
        PushControlWidth();
        UIKeyBinding("##globalRefreshKey", &OW::Config::CacheRefreshKey, KeyBindingStorage::VirtualKey);
        ImGui::PopItemWidth();
        if (OW::Config::CacheRefreshKey == OW::Config::MenuToggleKey)
            OW::Config::CacheRefreshKey = 0;
    });
}

namespace {

struct PredictionActionCard {
    const PredictionProfile* profile;
    const OW::WeaponSpec* weapon = nullptr;
    const OW::HeroSkillDefinition* skill = nullptr;
};

ID3D11ShaderResourceView* PredictionWeaponIcon(const HeroOption& hero, const OW::WeaponSpec& weapon) {
    auto* icons = Render::GetIconManager();
    if (!icons) return nullptr;

    std::string iconSlug;
    for (unsigned char ch : weapon.weaponName) {
        if (std::isalnum(ch)) {
            iconSlug += static_cast<char>(std::tolower(ch));
        } else if (ch != '\'' && !iconSlug.empty() && iconSlug.back() != '-') {
            iconSlug += '-';
        }
    }
    if (!iconSlug.empty() && iconSlug.back() == '-') iconSlug.pop_back();
    if (weapon.weaponId == "cassidy_ads_perk") iconSlug = "peacekeeper";
    const std::string heroSlug = OW::HeroDisplayNameToSlug(hero.label);
    const std::string key = heroSlug + "/" + iconSlug;
    if (auto* icon = icons->GetIcon(key)) return icon;
    icons->LoadAbilityIcons(heroSlug, {iconSlug});
    return icons->GetIcon(key);
}

std::vector<PredictionActionCard> PredictionActionCards(
    const HeroOption& hero, const std::vector<PredictionProfile>& profiles) {
    const auto findProfile = [&](std::string_view id) -> const PredictionProfile* {
        const auto found = std::lower_bound(profiles.begin(), profiles.end(), id,
            [](const PredictionProfile& profile, std::string_view id) { return profile.profileId < id; });
        return found != profiles.end() && found->profileId == id ? &*found : nullptr;
    };
    std::vector<PredictionActionCard> cards;
    for (auto weapon = OW::WeaponSpecsBegin(); weapon != OW::WeaponSpecsEnd(); ++weapon) {
        if (weapon->heroId != hero.heroId || weapon->weaponId.empty()) continue;
        if (const auto* profile = findProfile(weapon->weaponId)) cards.push_back({profile, weapon});
    }
    std::stable_sort(cards.begin(), cards.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.weapon->order < rhs.weapon->order;
    });
    const auto listed = [&](const PredictionProfile* profile) {
        return std::any_of(cards.begin(), cards.end(),
            [&](const auto& card) { return card.profile == profile; });
    };
    const auto skillStart = cards.size();
    for (const auto& skill : OW::AllHeroSkillDefinitions()) {
        if (skill.heroId != hero.heroId || !skill.skillId || !*skill.skillId) continue;
        if (const auto* profile = findProfile(std::string("skill:") + skill.skillId);
            profile && (!profile->heroId || profile->heroId == hero.heroId) && !listed(profile)) {
            cards.push_back({profile, nullptr, &skill});
        }
    }
    std::stable_sort(cards.begin() + skillStart, cards.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.skill->inputAction < rhs.skill->inputAction;
    });
    for (const auto& profile : profiles) {
        if (profile.heroId == hero.heroId && !listed(&profile)) cards.push_back({&profile});
    }
    return cards;
}

const char* PredictionActionTitle(const PredictionActionCard& card) {
    if (card.weapon) return L(card.weapon->weaponName.data());
    if (card.skill && card.skill->displayName) return L(card.skill->displayName);
    return card.profile->profileId.c_str();
}

} // namespace

void UI::DrawPredictionDataPage() {
    const bool zh = OW::Config::uiLanguage == OW::Config::kUiLanguageChineseSimplified;
    const auto& hero = DrawPredictionHeroPicker();
    float defaultRadius = OW::kDefaultInstantHitRadius;
    const auto profiles = OW::SnapshotPredictionProfiles(&defaultRadius);
    const auto cards = PredictionActionCards(hero, profiles);
    UIGroupBox(zh ? "预测数据" : "Prediction Data");
    SettingRow(zh ? "即时命中公共半径" : "Default instant-hit radius");
    PushControlWidth();
    if (UISlider("##instantHitDefaultRadius", &defaultRadius, 0.0f,
                 OW::kMaxPredictionProjectileRadius, Number(3, " m")))
        OW::SetInstantHitDefaultRadius(defaultRadius);
    ImGui::PopItemWidth();
    ImGui::Spacing();
    if (cards.empty()) {
        ImGui::TextDisabled("%s", zh ? "当前英雄还没有预测数据" : "No prediction data for this hero.");
        CloseGroupBox();
        return;
    }

    static std::string selectedProfileId;
    auto selected = std::find_if(cards.begin(), cards.end(),
        [](const auto& card) { return card.profile->profileId == selectedProfileId; });
    if (selected == cards.end()) {
        selected = cards.begin();
        selectedProfileId = selected->profile->profileId;
    }

    const float iconSide = ImGui::GetFontSize() * 2.0f;
    SettingRow(zh ? "武器与技能" : "Weapon / Skill");
    const float stripContentWidth = static_cast<float>(cards.size()) *
        (iconSide + 2.0f * ImGui::GetStyle().FramePadding.x + ImGui::GetStyle().ItemSpacing.x);
    const bool needsScrollbar = stripContentWidth > ImGui::GetContentRegionAvail().x;
    const float rowHeight = iconSide + 2.0f * ImGui::GetStyle().FramePadding.y +
        (needsScrollbar ? ImGui::GetStyle().ScrollbarSize + 6.0f : 2.0f);
    ImGui::PushID(static_cast<int>(&hero - kHeroOptions));
    if (ImGui::BeginChild("##predictionActions", ImVec2(0.0f, rowHeight), false,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        for (auto card = cards.begin(); card != cards.end(); ++card) {
            if (card != cards.begin()) ImGui::SameLine();
            ImGui::PushID(card->profile->profileId.c_str());
            const bool active = card == selected;
            if (active) ImGui::PushStyleColor(ImGuiCol_Button,
                ImGui::ColorConvertU32ToFloat4(kColAccentDark));
            const char* title = PredictionActionTitle(*card);
            auto* icon = card->weapon ? PredictionWeaponIcon(hero, *card->weapon)
                : card->skill ? HeroSkillIconForDefinition(*card->skill) : nullptr;
            const bool pressed = icon
                ? ImGui::ImageButton(title, reinterpret_cast<ImTextureID>(icon), ImVec2(iconSide, iconSide))
                : ImGui::Button(title, ImVec2(0.0f, iconSide + 2.0f * ImGui::GetStyle().FramePadding.y));
#ifdef IMGUI_ENABLE_TEST_ENGINE
            ImGuiContext& g = *ImGui::GetCurrentContext();
            IMGUI_TEST_ENGINE_ITEM_INFO(ImGui::GetItemID(), title, g.LastItemData.StatusFlags);
#endif
            if (active) ImGui::PopStyleColor();
            ImGui::SetItemTooltip("%s", title);
            if (pressed) {
                selected = card;
                selectedProfileId = card->profile->profileId;
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::PopID();

    PredictionProfile profile = *selected->profile;
    ImGui::PushID(profile.profileId.c_str());
    SettingRow(zh ? "当前动作" : "Active action");
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(PredictionActionTitle(*selected));
    ImGui::SameLine(0, 10);
    ImGui::TextDisabled("(%s %d)", zh ? "配置版本" : "Config version", profile.configVersion);
    bool changed = false;

    SettingRow(zh ? "投射速度" : "Projectile speed");
    PushControlWidth();
    changed |= UISlider("##predictionSpeed", &profile.projectile.projectileSpeed, 0.0f, 500.0f, Number(2, " m/s"));
    ImGui::PopItemWidth();
    if (profile.inheritInstantHitRadius.has_value()) {
        SettingRow(zh ? "半径来源" : "Radius source");
        const char* radiusSources[] = { zh ? "继承公共半径" : "Inherit default", zh ? "动作单独覆盖" : "Override for this action" };
        int source = *profile.inheritInstantHitRadius ? 0 : 1;
        PushControlWidth();
        if (UISelect("##predictionRadiusSource", &source, radiusSources, IM_ARRAYSIZE(radiusSources))) {
            profile.inheritInstantHitRadius = source == 0;
            profile.projectile.projectileRadius = defaultRadius;
            changed = true;
        }
        ImGui::PopItemWidth();
    }
    SettingRow(zh ? "投射物半径" : "Projectile radius");
    if (profile.inheritInstantHitRadius.value_or(false)) {
        ImGui::Text("%.3f m", defaultRadius);
    } else {
        PushControlWidth();
        changed |= UISlider("##predictionRadius", &profile.projectile.projectileRadius, 0.0f,
                            OW::kMaxPredictionProjectileRadius, Number(3, " m"));
        ImGui::PopItemWidth();
    }
    SettingRow(zh ? "重力" : "Gravity");
    changed |= UICheckbox("##predictionGravity", &profile.projectile.gravity);
    if (profile.projectile.gravity) {
        SettingRow(zh ? "重力加速度" : "Gravity acceleration");
        PushControlWidth();
        changed |= UISlider("##predictionGravityAcceleration", &profile.projectile.gravityAcceleration, 0.0f, 30.0f, Number(2, " m/s²"));
        ImGui::PopItemWidth();
    }
    SettingRow(zh ? "施法延迟" : "Cast delay");
    PushControlWidth();
    changed |= UISlider("##predictionCastDelay", &profile.projectile.castDelayMs, 0.0f, 1000.0f, Number(0, " ms"));
    ImGui::PopItemWidth();
    SettingRow(zh ? "发射方向修正" : "Launch direction bias");
    PushControlWidth();
    changed |= UISlider("##predictionUpBias", &profile.projectile.launchDirectionUpBias, -1.0f, 1.0f, Number(3));
    ImGui::PopItemWidth();
    const bool hasChargeSpeed = profile.projectile.chargeMinSpeed > 0.0f || profile.projectile.chargeMaxSpeed > 0.0f ||
        (selected->weapon && selected->weapon->projectile.chargeMaxSpeed > 0.0f);
    if (hasChargeSpeed) {
        SettingRow(zh ? "最低蓄力速度" : "Minimum charge speed");
        PushControlWidth();
        changed |= UISlider("##predictionChargeMin", &profile.projectile.chargeMinSpeed, 0.0f, 500.0f, Number(2, " m/s"));
        ImGui::PopItemWidth();
        SettingRow(zh ? "最高蓄力速度" : "Maximum charge speed");
        PushControlWidth();
        changed |= UISlider("##predictionChargeMax", &profile.projectile.chargeMaxSpeed, 0.0f, 500.0f, Number(2, " m/s"));
        ImGui::PopItemWidth();
        if (profile.projectile.chargeMaxSpeed < profile.projectile.chargeMinSpeed)
            profile.projectile.chargeMaxSpeed = profile.projectile.chargeMinSpeed;
    }
    if (changed)
        OW::UpdatePredictionProfile(profile);
    ImGui::PopID();
    CloseGroupBox();
}
