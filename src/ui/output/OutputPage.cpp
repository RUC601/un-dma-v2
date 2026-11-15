#include "DevicePanels.hpp"
#include "ui/EditorSupport.hpp"
#include "output/DeviceCapabilities.hpp"
#include "output/OutputRuntime.hpp"
#include "output/AutomaticDeviceSelection.hpp"
#include "Game/Target.hpp"
#include "features/aim/BehaviorController.hpp"
#include "Utils/Diagnostics.hpp"
#include <cstdio>

using namespace UI::Theme;

void DrawMiscOutputPage() {
    const auto device=V2::Output::Describe(V2::Output::ActiveRuntimeSnapshot().descriptor);
    ImGui::TextWrapped("%s · 移动 %s / 鼠标 %s / 键盘 %s / 物理监测 %s",
        device.name,device.move?"可用":"不支持",device.buttons?"可用":"不支持",
        device.keyboard?"可用":"不支持",device.physicalMonitor?"可用":"不支持");
    ImGui::TextWrapped("%s", L("Device rate is not measured in this build."));
    ImGui::TextWrapped("%s", L("MAKCU Controller / Medius: unavailable until their action mapping and protocol are established."));
    ImGui::Spacing();

    const auto physical=V2::Output::ActivePhysicalInput();
    UI::OutputSettings::DrawDeviceNotes(OW::Config::outputDeviceType, physical.available);
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
        if (ImGui::Checkbox("##Enable", &OW::Config::outputEnabled)) {
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
        if (ImGui::Checkbox("##AutoDetectDevice", &OW::Config::outputAutoDetect))
            markOutputRuntimeConfigChanged();
        ImGui::TextWrapped("%s", L("Each device keeps its connection, calibration and delay settings. A working device stays selected; after disconnecting, saved devices are checked again."));
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

        UI::OutputSettings::DrawConnectionFields(
            OW::Config::outputDeviceType, markOutputRuntimeConfigChanged);
        if (OW::Config::outputAutoDetect && UIActionButton(L("Detect again"), 104.0f))
            markOutputRuntimeConfigChanged();

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
