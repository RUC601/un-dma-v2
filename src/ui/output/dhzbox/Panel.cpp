#include "ui/output/DevicePanels.hpp"
#include "ui/EditorSupport.hpp"
using namespace UI::Theme;

namespace UI::OutputSettings::DhzBox {
void DrawFields(const ConfigChanged& changed) {
    SettingRow("DHZ Box IP");
    ImGui::InputText("##DhzIp", OW::Config::dhzBoxIp, IM_ARRAYSIZE(OW::Config::dhzBoxIp));
    if (ImGui::IsItemDeactivatedAfterEdit()) changed();
    SettingRow("DHZ Box 端口");
    ImGui::InputInt("##DhzPort", &OW::Config::dhzBoxPort);
    if (ImGui::IsItemDeactivatedAfterEdit()) changed();
    SettingRow("DHZ 随机偏移");
    ImGui::SliderInt("##DhzShift", &OW::Config::dhzBoxRandomShift, 0, 25);
    if (ImGui::IsItemDeactivatedAfterEdit()) changed();
    SettingRow("DHZ 监听端口");
    ImGui::InputInt("##DhzMonitor", &OW::Config::dhzBoxMonitorPort);
    if (ImGui::IsItemDeactivatedAfterEdit()) changed();
    ImGui::TextWrapped("DHZ 使用 UDP Caesar 字母偏移；随机偏移按盒子屏幕上的 Rand 设置。默认端口通常为 8888。 ");
}
void DrawNotes(bool physicalAvailable) {
    ImGui::TextWrapped("DHZ Box 已接入 UDP 控制与状态监听；实机待验证。状态监听：%s", physicalAvailable ? "当前有效" : "暂无有效样本");
}
}
