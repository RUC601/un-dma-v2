#include "ui/output/DevicePanels.hpp"
#include "ui/EditorSupport.hpp"
using namespace UI::Theme;

namespace UI::OutputSettings::Ferrum {
void DrawFields(const ConfigChanged& changed)
{
    DrawSerialPortField(changed);
}
void DrawNotes(bool physicalAvailable)
{
    ImGui::TextWrapped("Ferrum：释放会强制松开按键约 125–175 ms 后恢复物理状态；连接经 Ferrum App，实机待验证。");
    ImGui::TextWrapped("软件实现已接入；实机待验证。状态监听：%s", physicalAvailable ? "当前有效" : "暂无有效样本");
}
}
