#include "ui/output/DevicePanels.hpp"
#include "ui/EditorSupport.hpp"
using namespace UI::Theme;

namespace UI::OutputSettings {
void DrawSerialPortField(const ConfigChanged& changed)
{
    SettingRow(T(UiText::ComPort));
    PushControlWidth();
    ImGui::InputText("##ComPort", OW::Config::outputComPort, IM_ARRAYSIZE(OW::Config::outputComPort));
    if (ImGui::IsItemDeactivatedAfterEdit())
        changed();
    ImGui::PopItemWidth();
}
}
