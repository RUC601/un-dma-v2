#include "DevicePanels.hpp"
#include "ui/EditorSupport.hpp"
#include "output/DeviceCapabilities.hpp"
#include "output/OutputRuntime.hpp"
#include "output/AutomaticDeviceSelection.hpp"
#include "Game/Target.hpp"
#include "features/aim/BehaviorController.hpp"
#include "Utils/Diagnostics.hpp"
#include <imgui_internal.h>
#include <cstdio>

using namespace UI::Theme;

void DrawMiscOutputPage() {
    UIGroupBox(T(UiText::GroupOutputSettings));
    {
        bool outputRuntimeReinitRequested = false;
        static bool outputConnectionTestOk = false;
        static std::string outputConnectionTestMessage;
        static bool gameSensitivityReadOk = false;
        static std::string gameSensitivityReadMessage;
        auto markOutputRuntimeConfigChanged = [&]() {
            outputConnectionTestMessage.clear();
            UI::OutputSettings::ResetDeviceFeedback();
            if (OW::Config::outputEnabled)
                outputRuntimeReinitRequested = true;
        };
        ImGui::PushID("OutputSettings");

        SettingRow(T(UiText::EnableOutput));
        const bool wasOutputEnabled = OW::Config::outputEnabled;
        if (UICheckbox("##Enable", &OW::Config::outputEnabled)) {
            UI::OutputSettings::ResetDeviceFeedback();
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

        SettingRow(L("Automatically select connected device"));
        if (UICheckbox("##AutoDetectDevice", &OW::Config::outputAutoDetect))
            markOutputRuntimeConfigChanged();
        SettingTooltip(L("Each device keeps its connection, calibration and delay settings. A working device stays selected; after disconnecting, saved devices are checked again."));
        const auto detection = V2::Output::GetAutomaticDeviceStatus();
        if (OW::Config::outputAutoDetect && !detection.message.empty())
            ImGui::TextWrapped("%s", L(detection.message.c_str()));

        SettingRow(T(UiText::DeviceType));
        PushControlWidth();
        int selectedDevice = OW::Config::outputDeviceType;
        if (UI::OutputSettings::SelectDevice("##DeviceType", selectedDevice)) {
            OW::Config::SelectOutputDeviceProfile(selectedDevice);
            markOutputRuntimeConfigChanged();
        }
        ImGui::PopItemWidth();
        if (BeginSettingTooltip()) {
            const auto device = V2::Output::Describe(V2::Output::ActiveRuntimeSnapshot().descriptor);
            ImGui::TextWrapped("%s · 移动 %s / 鼠标 %s / 键盘 %s / 物理监测 %s",
                device.name,device.move?"可用":"不支持",device.buttons?"可用":"不支持",
                device.keyboard?"可用":"不支持",device.physicalMonitor?"可用":"不支持");
            ImGui::TextWrapped("身份 %s · %s · 能力来源 %s",
                device.identityConfirmed ? "已确认" : "未确认",
                device.identityTag ? device.identityTag : "unknown",
                device.capabilitySource ? device.capabilitySource : "unknown");
            ImGui::TextWrapped("%s", L("Device rate is not measured in this build."));
            ImGui::TextWrapped("%s", L("MAKCU Controller / Medius: unavailable until their action mapping and protocol are established."));
            UI::OutputSettings::DrawDeviceNotes(OW::Config::outputDeviceType,
                                               V2::Output::ActivePhysicalInput().available);
            EndSettingTooltip();
        }

        UI::OutputSettings::DrawConnectionFields(
            OW::Config::outputDeviceType, markOutputRuntimeConfigChanged);
        if (OW::Config::outputAutoDetect) {
            SettingRow(OW::Config::uiLanguage == 1 ? "连接探测" : "Device scan");
            if (UIActionButton(L("Detect again"), 104.0f))
                markOutputRuntimeConfigChanged();
        }

        SettingRow(T(UiText::BaseCountsRad));
        PushControlWidth();
        float baseCountsPerRadian = OW::Config::OutputBaseCountsPerRadian();
        const bool baseCountsChanged = ImGui::InputFloat(
            "##CountsPerRadian", &baseCountsPerRadian, 1.0f, 10.0f, "%.1f");
        if (ImGui::IsItemDeactivatedAfterEdit() ||
            (baseCountsChanged && !ImGui::IsItemActive())) {
            OW::Config::ApplyManualBaseCountsPerRadian(baseCountsPerRadian);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", L("Device relative mouse counts per radian. Calibration updates this value directly; editing it manually clears the previous calibration."));
        ImGui::PopItemWidth();

        if (OW::Config::ReadOutputConversionSnapshot().autoReadGameMouseSensitivity)
            OW::RefreshAutoGameMouseSensitivity();

        auto convSnapshot = OW::Config::ReadOutputConversionSnapshot();
        SettingRow(T(UiText::CurrentGameSens));
        {
            const bool autoReadActive =
                convSnapshot.autoReadGameMouseSensitivity &&
                convSnapshot.gameMouseSensitivityAutoDetected &&
                std::isfinite(convSnapshot.detectedGameSensitivity) &&
                convSnapshot.detectedGameSensitivity > 0.0f;
            const char* sourceTag = autoReadActive
                ? (OW::Config::uiLanguage == 1 ? "DMA 实时" : "DMA live")
                : (convSnapshot.autoReadGameMouseSensitivity
                    ? (OW::Config::uiLanguage == 1 ? "手动回退" : "Manual fallback")
                    : nullptr);
            if (sourceTag) {
                const float tagWidth = ImGui::CalcTextSize(sourceTag).x + 8.0f;
                const float tagSpacing = ImGui::GetStyle().ItemSpacing.x;
                const float sensWidth = MaxFloat(
                    80.0f,
                    ImGui::GetContentRegionAvail().x - tagWidth - tagSpacing - kControlRightPadding);
                ImGui::PushItemWidth(sensWidth);
            } else {
                PushControlWidth();
            }
            if (autoReadActive) {
                float liveSensitivity = convSnapshot.effectiveGameSensitivity;
                ImGui::InputFloat("##GameSensLive", &liveSensitivity,
                                  0.0f, 0.0f, "%.2f",
                                  ImGuiInputTextFlags_ReadOnly);
            } else {
                float manualSensitivity = convSnapshot.manualGameSensitivity;
                const bool manualChanged = ImGui::InputFloat("##GameSens", &manualSensitivity,
                                  0.0f, 0.0f, "%.2f");
                if (ImGui::IsItemDeactivatedAfterEdit() ||
                    (manualChanged && !ImGui::IsItemActive())) {
                    OW::Config::SetManualGameMouseSensitivity(manualSensitivity);
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", L("Current in-game sensitivity used for scaling and calibration. Shows live DMA value when Auto Read is active; otherwise editable manually."));
            ImGui::PopItemWidth();
            if (sourceTag) {
                ImGui::SameLine();
                ImGui::TextColored(
                    autoReadActive ? ImVec4(0.30f, 0.90f, 0.45f, 1.0f)
                                   : ImVec4(0.65f, 0.65f, 0.70f, 1.0f),
                    "%s",
                    sourceTag);
            }
        }

        SettingRow(T(UiText::AutoReadGameSens));
        bool autoReadEnabled = convSnapshot.autoReadGameMouseSensitivity;
        if (UICheckbox("##AutoReadGameSens", &autoReadEnabled)) {
            OW::Config::SetAutoReadGameMouseSensitivity(autoReadEnabled);
            gameSensitivityReadMessage.clear();
            if (autoReadEnabled) {
                gameSensitivityReadOk = OW::RefreshAutoGameMouseSensitivity(true);
                const auto refreshed = OW::Config::ReadOutputConversionSnapshot();
                char buffer[96] = {};
                std::snprintf(buffer, sizeof(buffer),
                    gameSensitivityReadOk ? "live %.2f" : "not available",
                    refreshed.detectedGameSensitivity);
                gameSensitivityReadMessage = buffer;
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
                if (autoReadEnabled) {
                    OW::Config::SetAutoDetectedGameMouseSensitivity(detectedSensitivity, true);
                } else {
                    OW::Config::SetManualGameMouseSensitivity(detectedSensitivity);
                    OW::Config::SetAutoDetectedGameMouseSensitivity(0.0f, false);
                }
                std::snprintf(buffer, sizeof(buffer), "read %.2f", detectedSensitivity);
                Diagnostics::Aim("game_sens.manual_read value=%.6f object=0x%llX autoRead=%d",
                    detectedSensitivity,
                    static_cast<unsigned long long>(sourceObject),
                    autoReadEnabled ? 1 : 0);
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

        convSnapshot = OW::Config::ReadOutputConversionSnapshot();
        SettingRow(T(UiText::ReferenceGameSens));
        const float useCurrentButtonWidth = 88.0f;
        const float useCurrentSpacing = ImGui::GetStyle().ItemSpacing.x;
        const float referenceWidth = MaxFloat(
            80.0f,
            ImGui::GetContentRegionAvail().x - useCurrentButtonWidth - useCurrentSpacing - kControlRightPadding);
        ImGui::PushItemWidth(referenceWidth);
        float referenceSensitivity = convSnapshot.referenceGameSensitivity;
        const bool referenceChanged = ImGui::InputFloat("##ReferenceGameSens", &referenceSensitivity,
                          0.0f, 0.0f, "%.2f");
        if (ImGui::IsItemDeactivatedAfterEdit() ||
            (referenceChanged && !ImGui::IsItemActive())) {
            OW::Config::SetReferenceGameSensitivity(referenceSensitivity);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", L("The in-game sensitivity used when Base Counts/Rad or calibration was measured. Used only when Auto Sens Scale is enabled."));
        ImGui::PopItemWidth();
        ImGui::SameLine();
        if (ImGui::Button(T(UiText::UseCurrent), ImVec2(useCurrentButtonWidth, ControlHeight()))) {
            OW::Config::SetReferenceGameSensitivity(OW::Config::EffectiveGameMouseSensitivity());
        }

        SettingRow(T(UiText::AutoSensScale));
        bool autoScaleEnabled = convSnapshot.autoScaleByGameSensitivity;
        if (UICheckbox("##AutoScaleGameSens", &autoScaleEnabled))
            OW::Config::SetAutoScaleByGameSensitivity(autoScaleEnabled);
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
        ImGui::GetCurrentWindow()->WorkRect.Max.x -= kControlRightPadding;
        const bool advOpen = ImGui::CollapsingHeader(L("Advanced device tools"));
        ImGui::GetCurrentWindow()->WorkRect.Max.x += kControlRightPadding;
        if (advOpen) {
        SettingRow(T(UiText::DebugLogging));
        UICheckbox("##Debug", &OW::Config::outputDebugLog);

        UI::OutputSettings::DrawDiagnosticTools(OW::Config::outputDeviceType);

        // ---- Counts-per-radian auto-calibration button ----
        SettingRow(T(UiText::CountsCalibration));
        {
            static std::string calibrationStatus;
            static bool calibrationFailed = false;
            const bool wasCalibrated = OW::Config::calibratedCountsPerRadian > 0.0f;
            if (ImGui::Button(OW::Config::calibrationInProgress ? T(UiText::Calibrating) : T(UiText::Calibrate),
                              ImVec2(72.0f, ControlHeight()))) {
                if (!OW::Config::calibrationInProgress) {
                    if (OW::Config::autoReadGameMouseSensitivity)
                        OW::RefreshAutoGameMouseSensitivity(true);
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

        UI::OutputSettings::DrawMaintenanceTools(OW::Config::outputDeviceType);

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
