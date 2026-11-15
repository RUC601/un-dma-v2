#pragma once
#include "output/DeviceMaintenance.hpp"

namespace V2::Output::KmBoxNetConfig {
struct Settings;
MaintenanceResult AllowMonitorFirewall(const Settings& settings);
MaintenanceResult RestartNetworkAdapter(const Settings& settings);
}
