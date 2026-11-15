#pragma once

#include <string>

namespace V2::Output {

struct MaintenanceResult {
    bool ok = false;
    std::string message;
};

// Explicit user actions from the device settings page.
MaintenanceResult AllowConfiguredMonitorFirewall(int legacyDeviceId);
MaintenanceResult RestartConfiguredNetworkAdapter(int legacyDeviceId);

}
