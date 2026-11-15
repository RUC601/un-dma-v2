#include "ui/output/DevicePanels.hpp"
#include "ui/EditorSupport.hpp"
using namespace UI::Theme;

namespace UI::OutputSettings::Makcu {
void DrawFields(const ConfigChanged& changed)
{
    DrawSerialPortField(changed);
}
void DrawNotes(bool physicalAvailable)
{
    ImGui::TextWrapped("软件实现已接入；实机待验证。状态监听：%s", physicalAvailable ? "当前有效" : "暂无有效样本");
}
}
