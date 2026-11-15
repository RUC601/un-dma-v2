#include "ui/output/DevicePanels.hpp"
#include "ui/EditorSupport.hpp"
using namespace UI::Theme;

namespace UI::OutputSettings {
void DrawSerialPortField(const ConfigChanged& changed)
{
    SettingRow(T(UiText::ComPort));
    PushControlWidth();
    ImGui::InputText("##ComPort", OW::Config::outputComPort, IM_ARRAYSIZE(OW::Config::outputComPort));
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        OW::Config::outputSerialInstanceId[0] = '\0';
        changed();
    }
    ImGui::PopItemWidth();
}
}
