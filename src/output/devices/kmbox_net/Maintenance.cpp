#include "Maintenance.hpp"
#include "Settings.hpp"
#include "output/platform/NetworkMaintenance.hpp"
#include "Utils/Diagnostics.hpp"

namespace V2::Output::KmBoxNetConfig {
MaintenanceResult AllowMonitorFirewall(const Settings& settings)
{
    const int monitorPort = EffectiveMonitorPort(settings);
    const auto result = Platform::AllowInboundUdpForCurrentExecutable(
        monitorPort, L"UNDMA CN KMBox Monitor UDP " + std::to_wstring(monitorPort));
    if (result.ok) {
        Diagnostics::Info("KMBox monitor firewall allow requested. effectiveMonitorPort=%d commandPort=%d manualOverride=%d",
            monitorPort, settings.commandPort, settings.manualMonitorPort ? 1 : 0);
        Diagnostics::Aim("output.firewall allow_requested effectiveMonitorPort=%d commandPort=%d manualOverride=%d",
            monitorPort, settings.commandPort, settings.manualMonitorPort ? 1 : 0);
    }
    return result;
}

MaintenanceResult RestartNetworkAdapter(const Settings& settings)
{
    return Platform::RestartAdapterForTargetIp(settings.ip, "USB 2.0 Ethernet Adapter");
}
}
