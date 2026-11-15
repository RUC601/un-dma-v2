#pragma once
#include "output/DeviceMaintenance.hpp"
#include <string>

namespace V2::Output::Platform {
MaintenanceResult AllowInboundUdpForCurrentExecutable(int monitorPort, const std::wstring& ruleName);
MaintenanceResult RestartAdapterForTargetIp(const char* ip, const char* fallbackDescription);
}
