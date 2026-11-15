#include "ui/output/DevicePanels.hpp"
#include "ui/EditorSupport.hpp"
using namespace UI::Theme;

#include "output/DeviceMaintenance.hpp"

namespace UI::OutputSettings::KmBoxNet {
namespace {
bool outputNetworkRestartOk = false;
std::string outputNetworkRestartMessage;
bool outputFirewallOk = false;
std::string outputFirewallMessage;
}
void ResetFeedback()
{
    outputNetworkRestartMessage.clear();
    outputFirewallMessage.clear();
}
void DrawFields(const ConfigChanged& changed)
{
    SettingRow(T(UiText::Ip));
    PushControlWidth();
    ImGui::InputText("##Ip", OW::Config::outputIp, IM_ARRAYSIZE(OW::Config::outputIp));
    if (ImGui::IsItemDeactivatedAfterEdit())
        changed();
    ImGui::PopItemWidth();

    SettingRow(T(UiText::Port));
    PushControlWidth();
    ImGui::InputInt("##Port", &OW::Config::outputPort, 0, 0);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        OW::Config::NormalizeOutputPorts();
        changed();
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
            changed();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", L("Advanced override. Auto mode uses the recommended monitor port derived from the command port."));

        if (OW::Config::outputMonitorPortManualOverride) {
            SettingRow(T(UiText::MonitorPort));
            PushControlWidth();
            ImGui::InputInt("##ManualMonitorPort", &OW::Config::outputMonitorPort, 0, 0);
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                OW::Config::NormalizeOutputPorts();
                changed();
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
        changed();
    ImGui::PopItemWidth();
}
void DrawMaintenance()
{
    {
        SettingRow(T(UiText::RestartNic));
        if (UIActionButton(T(UiText::RestartNic), 104.0f)) {
            const auto restartResult = V2::Output::RestartConfiguredNetworkAdapter(OW::Config::outputDeviceType);
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
    {
        SettingRow(T(UiText::MonitorFirewall));
        if (UIActionButton(T(UiText::FixFirewall), 112.0f)) {
            const auto firewallResult = V2::Output::AllowConfiguredMonitorFirewall(OW::Config::outputDeviceType);
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
}
