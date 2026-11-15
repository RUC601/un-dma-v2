#include "ui/output/DevicePanels.hpp"
#include "ui/EditorSupport.hpp"
using namespace UI::Theme;

#include "output/MockDiagnostics.hpp"

namespace UI::OutputSettings::Mock {
namespace {
constexpr const char* kMockFaultModes[] = {"None", "Output Timeout", "Drop Output"};
}
void DrawFields(const ConfigChanged&)
{
    const V2::Output::MockHardwareSnapshot snapshot = V2::Output::SnapshotMock();
    SettingRow(T(UiText::MockStatus));
    ImGui::TextColored(
        snapshot.initialized ? ImVec4(0.30f, 0.90f, 0.45f, 1.0f)
                             : ImVec4(0.85f, 0.65f, 0.25f, 1.0f),
        "%s",
        snapshot.initialized ? T(UiText::Ready) : T(UiText::NotInitialized));
}
void DrawDiagnostics()
{
    V2::Output::MockHardwareSnapshot snapshot = V2::Output::SnapshotMock();

    SettingRow(T(UiText::MockFault));
    PushControlWidth();
    int faultMode = static_cast<int>(snapshot.faultMode);
    if (UISelect("##MockFaultMode", &faultMode, kMockFaultModes, IM_ARRAYSIZE(kMockFaultModes))) {
        faultMode = std::clamp(faultMode, 0, IM_ARRAYSIZE(kMockFaultModes) - 1);
        V2::Output::SetMockFaultMode(static_cast<V2::Output::MockFaultMode>(faultMode));
        snapshot = V2::Output::SnapshotMock();
    }
    ImGui::PopItemWidth();

    SettingRow(T(UiText::MockReset));
    if (UIActionButton(T(UiText::Reset), 72.0f))
        V2::Output::ResetMock();

    snapshot = V2::Output::SnapshotMock();
    SettingRow(T(UiText::MockStats));
    ImGui::Text("events=%llu moves=%llu buttons=%llu keys=%llu masks=%llu out=0x%02X masked=0x%02X",
        snapshot.totalEvents,
        snapshot.moveEvents,
        snapshot.buttonEvents,
        snapshot.keyboardEvents,
        snapshot.maskEvents,
        snapshot.outputMouseButtons,
        snapshot.maskedButtons);
}
}
