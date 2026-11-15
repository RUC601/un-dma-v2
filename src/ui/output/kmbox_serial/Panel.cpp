#include "ui/output/DevicePanels.hpp"
namespace UI::OutputSettings::KmBoxSerial {
void DrawFields(const ConfigChanged& changed)
{
    DrawSerialPortField(changed);
}
}
