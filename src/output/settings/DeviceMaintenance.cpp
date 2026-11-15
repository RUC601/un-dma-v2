#include "output/DeviceMaintenance.hpp"
#include "output/DeviceRegistry.hpp"
#include "output/DeviceSettings.hpp"
#include "output/devices/kmbox_net/Maintenance.hpp"

namespace V2::Output {
MaintenanceResult AllowConfiguredMonitorFirewall(int legacyDeviceId)
{
    const auto& choice = DeviceChoiceForId(legacyDeviceId);
    if (choice.legacyId != legacyDeviceId || choice.settingsKind != SettingsKind::Network)
        return {false, "Network mode only"};
    return KmBoxNetConfig::AllowMonitorFirewall(DeviceSettings::Network());
}

MaintenanceResult RestartConfiguredNetworkAdapter(int legacyDeviceId)
{
    const auto& choice = DeviceChoiceForId(legacyDeviceId);
    if (choice.legacyId != legacyDeviceId || choice.settingsKind != SettingsKind::Network)
        return {false, "Network mode only"};
    return KmBoxNetConfig::RestartNetworkAdapter(DeviceSettings::Network());
}
}
