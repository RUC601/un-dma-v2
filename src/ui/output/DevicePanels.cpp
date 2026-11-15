#include "DevicePanels.hpp"
#include "ui/EditorSupport.hpp"
#include "output/DeviceRegistry.hpp"
#include <array>
#include <vector>

namespace UI::OutputSettings {
namespace {
using V2::Output::DeviceId;

struct Panel {
    DeviceId device;
    void (*fields)(const ConfigChanged&);
    void (*notes)(bool) = nullptr;
    void (*diagnostics)() = nullptr;
    void (*maintenance)() = nullptr;
    void (*resetFeedback)() = nullptr;
};

// UI composition is the only place that binds device identity to a form.
constexpr std::array panels{
    Panel{DeviceId::KmBoxNet, KmBoxNet::DrawFields, nullptr, nullptr,
        KmBoxNet::DrawMaintenance, KmBoxNet::ResetFeedback},
    Panel{DeviceId::KmBoxSerial, KmBoxSerial::DrawFields},
    Panel{DeviceId::Mock, Mock::DrawFields, nullptr, Mock::DrawDiagnostics},
    Panel{DeviceId::Makcu4M, Makcu::DrawFields, Makcu::DrawNotes},
    Panel{DeviceId::Makcu115200, Makcu::DrawFields, Makcu::DrawNotes},
    Panel{DeviceId::Ferrum, Ferrum::DrawFields, Ferrum::DrawNotes},
    Panel{DeviceId::CatBox, CatBox::DrawFields, CatBox::DrawNotes},
};

const Panel* FindPanel(int legacyDeviceId)
{
    for (const auto& panel : panels) {
        if (static_cast<int>(panel.device) == legacyDeviceId)
            return &panel;
    }
    return nullptr;
}
}

bool SelectDevice(const char* id, int& legacyDeviceId)
{
    const auto choices = V2::Output::DeviceChoices();
    static const std::vector<const char*> labels = [] {
        std::vector<const char*> result;
        for (const auto& choice : V2::Output::DeviceChoices())
            result.push_back(choice.label);
        return result;
    }();
    int selected = 0;
    for (std::size_t index = 0; index < choices.size(); ++index) {
        if (choices[index].legacyId == legacyDeviceId)
            selected = static_cast<int>(index);
    }
    if (!UISelect(id, &selected, labels.data(), static_cast<int>(labels.size())))
        return false;
    legacyDeviceId = choices[static_cast<std::size_t>(selected)].legacyId;
    return true;
}

void DrawConnectionFields(int legacyDeviceId, const ConfigChanged& changed)
{
    if (const auto* panel = FindPanel(legacyDeviceId))
        panel->fields(changed);
}

void DrawDeviceNotes(int legacyDeviceId, bool physicalAvailable)
{
    if (const auto* panel = FindPanel(legacyDeviceId); panel && panel->notes)
        panel->notes(physicalAvailable);
}

void DrawDiagnosticTools(int legacyDeviceId)
{
    if (const auto* panel = FindPanel(legacyDeviceId); panel && panel->diagnostics)
        panel->diagnostics();
}

void DrawMaintenanceTools(int legacyDeviceId)
{
    if (const auto* panel = FindPanel(legacyDeviceId); panel && panel->maintenance)
        panel->maintenance();
}

void ResetDeviceFeedback()
{
    for (const auto& panel : panels) {
        if (panel.resetFeedback)
            panel.resetFeedback();
    }
}
}
