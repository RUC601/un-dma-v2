#pragma once

#include <functional>

namespace UI::OutputSettings {
using ConfigChanged = std::function<void()>;
bool SelectDevice(const char* id, int& legacyDeviceId);
void DrawConnectionFields(int legacyDeviceId, const ConfigChanged& changed);
void DrawDiagnosticTools(int legacyDeviceId);
void DrawMaintenanceTools(int legacyDeviceId);
void DrawDeviceNotes(int legacyDeviceId, bool physicalAvailable);
void ResetDeviceFeedback();

namespace KmBoxNet {
void DrawFields(const ConfigChanged& changed);
void DrawMaintenance();
void ResetFeedback();
}
namespace KmBoxSerial { void DrawFields(const ConfigChanged& changed); }
namespace Makcu {
void DrawFields(const ConfigChanged& changed);
void DrawNotes(bool physicalAvailable);
}
namespace Ferrum {
void DrawFields(const ConfigChanged& changed);
void DrawNotes(bool physicalAvailable);
}
namespace CatBox {
void DrawFields(const ConfigChanged& changed);
void DrawNotes(bool physicalAvailable);
}
namespace DhzBox {
void DrawFields(const ConfigChanged& changed);
void DrawNotes(bool physicalAvailable);
}
namespace Mock {
void DrawFields(const ConfigChanged& changed);
void DrawDiagnostics();
}
void DrawSerialPortField(const ConfigChanged& changed);
}
