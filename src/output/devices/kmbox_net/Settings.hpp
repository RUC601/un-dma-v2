#pragma once

namespace V2::Output::KmBoxNetConfig {

inline constexpr int kMinimumCommandPort = 1;
inline constexpr int kMaximumCommandPort = 65535;

// Public configuration model. Transport and monitor state stay in the backend.
struct Settings {
    char ip[32] = "";
    int commandPort = 8808;
    int monitorPort = 8809;
    bool manualMonitorPort = false;
    char mac[32] = "";
};

inline Settings current;

inline int RecommendedMonitorPort(int commandPort) noexcept
{
    if (commandPort >= 1 && commandPort < 65535)
        return commandPort + 1;
    if (commandPort == 65535)
        return commandPort - 1;
    return Settings{}.monitorPort;
}

inline int EffectiveMonitorPort(const Settings& settings) noexcept
{
    const int recommended = RecommendedMonitorPort(settings.commandPort);
    if (!settings.manualMonitorPort || settings.monitorPort < 1 ||
        settings.monitorPort > 65535 || settings.monitorPort == settings.commandPort)
        return recommended;
    return settings.monitorPort;
}

enum class MonitorAdjustment { None, Automatic, InvalidPort, CommandPortCollision };

inline MonitorAdjustment MonitorPortAdjustment(const Settings& settings) noexcept
{
    if (!settings.manualMonitorPort)
        return MonitorAdjustment::Automatic;
    if (settings.monitorPort < 1 || settings.monitorPort > 65535)
        return MonitorAdjustment::InvalidPort;
    if (settings.monitorPort == settings.commandPort)
        return MonitorAdjustment::CommandPortCollision;
    return MonitorAdjustment::None;
}

template<class ReadText, class ReadInteger, class ReadBoolean>
void Load(Settings& settings, ReadText readText, ReadInteger readInteger, ReadBoolean readBoolean)
{
    readText("kmboxIp", settings.ip);
    settings.commandPort = readInteger("kmboxPort", settings.commandPort);
    settings.monitorPort = readInteger("kmboxMonitorPort", RecommendedMonitorPort(settings.commandPort));
    settings.manualMonitorPort = readBoolean("kmboxMonitorPortManualOverride", false);
    readText("kmboxMac", settings.mac);
}

template<class WriteText, class WriteInteger, class WriteBoolean>
void Save(const Settings& settings, WriteText writeText, WriteInteger writeInteger, WriteBoolean writeBoolean)
{
    writeText("kmboxIp", settings.ip);
    writeInteger("kmboxPort", settings.commandPort);
    writeInteger("kmboxMonitorPort", settings.monitorPort);
    writeBoolean("kmboxMonitorPortManualOverride", settings.manualMonitorPort);
    writeText("kmboxMac", settings.mac);
}

}
