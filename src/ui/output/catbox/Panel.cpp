#include "ui/output/DevicePanels.hpp"
#include "ui/EditorSupport.hpp"
using namespace UI::Theme;

namespace UI::OutputSettings::CatBox {
void DrawFields(const ConfigChanged& changed)
{
    SettingRow("CatBox IP");
    ImGui::InputText("##CatIp", OW::Config::catBoxIp, IM_ARRAYSIZE(OW::Config::catBoxIp));
    if (ImGui::IsItemDeactivatedAfterEdit())
        changed();

    SettingRow("CatBox 端口");
    ImGui::InputInt("##CatPort", &OW::Config::catBoxPort);
    if (ImGui::IsItemDeactivatedAfterEdit())
        changed();

    SettingRow("CatBox UUID");
    ImGui::InputText("##CatUuid", OW::Config::catBoxUuid, IM_ARRAYSIZE(OW::Config::catBoxUuid));
    if (ImGui::IsItemDeactivatedAfterEdit())
        changed();

    SettingRow("CatBox 监听端口");
    ImGui::InputInt("##CatMonitor", &OW::Config::catBoxMonitorPort);
    if (ImGui::IsItemDeactivatedAfterEdit())
        changed();
}

void DrawNotes(bool physicalAvailable)
{
    ImGui::TextWrapped("软件实现已接入；实机待验证。状态监听：%s",
        physicalAvailable ? "当前有效" : "暂无有效样本");
}
}
